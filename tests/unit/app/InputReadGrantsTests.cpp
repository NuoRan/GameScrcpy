#include "ControlChannel.h"
#include "ExtensionProfile.h"
#include "InputHost.h"
#include "InputReadGrants.h"
#include "ProfileLibrary.h"
#include "ScriptRuntime.h"
#include "ScriptWorkbenchFiles.h"
#include "TaskLibrary.h"

#include <gs/input/Profile.h>

#include <doctest/doctest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantMap>

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace gs;
using namespace gs::app;
using namespace std::chrono_literals;

namespace {

class Application
{
public:
    Application()
    {
        if (QCoreApplication::instance() == nullptr) {
            m_app = std::make_unique<QCoreApplication>(m_argc, m_argv.data());
        }
    }

private:
    int m_argc = 1;
    char m_name[5] = "test";
    std::array<char*, 2> m_argv{m_name, nullptr};
    std::unique_ptr<QCoreApplication> m_app;
};

class QuietSink final : public ControlSink
{
public:
    bool send(const protocol::TouchFrame&) override { return true; }
    bool send(const protocol::Key&) override { return true; }
    bool send(const protocol::Scroll&) override { return true; }
    bool send(const protocol::Text&) override { return true; }
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

    QuietSink sink;
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

    void press(std::uint16_t makeCode)
    {
        host.key({.makeCode = makeCode}, now());
        host.key({.makeCode = makeCode, .released = true}, now());
    }

    InputHost host{{.pressedMouse = [] { return std::uint8_t{0}; }}};

private:
    std::mutex m_mutex;
    std::deque<Task> m_tasks;
    const std::chrono::steady_clock::time_point m_started = std::chrono::steady_clock::now();
};

class ReadingLane final : public LaneAccess
{
public:
    ReadingLane(Rig& rig, std::shared_ptr<ControlChannel> phone)
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

    [[nodiscard]] const ControlChannel* channel() const noexcept override { return m_phone.get(); }
    [[nodiscard]] std::optional<input::Vec2> cursor() const override { return input::Vec2{0.25, 0.75}; }
    [[nodiscard]] bool held(std::string_view name) const override { return name == "KeyW"; }

private:
    Rig& m_rig;
    std::shared_ptr<ControlChannel> m_phone;
};

constexpr std::uint16_t kSpace = 0x39;

constexpr std::string_view kProfile = R"({"format":"gamescrcpy.profile","version":3,
    "meta":{"name":"Reader"},
    "controls":[{"id":"jump","type":"hold","anchor":"bottomRight","offset":[-0.2,-0.2],"inputs":{"press":"Space"}}],
    "vars":[]})";

constexpr std::string_view kReadingLogic = R"(const read = (when) => {
    try {
        const at = input.cursor();
        log.info(when, at.x, at.y, input.isDown("KeyW"));
    } catch (error) {
        log.info(when, error.message.split(" ")[0]);
    }
};
read("load");
controls.jump.onPress(() => read("press"));
)";

constexpr std::string_view kLoadReadingLogic = R"(try {
    const at = input.cursor();
    log.info("load", at.x, at.y, input.isDown("KeyW"));
} catch (error) {
    log.info("load", error.message.split(" ")[0]);
}
)";

constexpr std::string_view kTask = R"(export default class Daily extends Task {
    async run() {}
}
)";

std::shared_ptr<const input::Profile> profileOf(std::string_view json)
{
    const input::LoadResult loaded = input::loadProfile(json);
    REQUIRE(loaded.ok());
    return std::make_shared<const input::Profile>(loaded.profile);
}

std::filesystem::path pathOf(const QString& text)
{
    return {text.toStdWString()};
}

void write(const QString& path, std::string_view text)
{
    REQUIRE(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(file.write(text.data(), static_cast<qint64>(text.size())) == static_cast<qint64>(text.size()));
}

void makeProfile(const QDir& root, const QString& name, std::string_view logic)
{
    write(root.filePath(name + QStringLiteral("/profile.json")), kProfile);
    if (!logic.empty()) {
        write(root.filePath(name + QStringLiteral("/logic.ts")), logic);
    }
}

QStringList listedIn(const QString& file, const QString& key)
{
    QFile opened(file);
    REQUIRE(opened.open(QIODevice::ReadOnly));
    QStringList ids;
    for (const QJsonValue& id : QJsonDocument::fromJson(opened.readAll()).object().value(key).toArray()) {
        ids.append(id.toString());
    }
    return ids;
}

ProfileLibrary::Discard deletingFolders()
{
    return [](const QString& path) {
        return QFileInfo(path).isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
    };
}

InputGrant profileGrant(const QString& id)
{
    return {.reader = InputReader::Profile, .id = id};
}

InputGrant taskGrant(const QString& id)
{
    return {.reader = InputReader::Task, .id = id};
}

class Data
{
public:
    Data() { REQUIRE(m_dir.isValid()); }

    [[nodiscard]] QString file(const QString& name) const { return m_dir.filePath(name); }
    [[nodiscard]] std::filesystem::path path(const QString& name) const { return pathOf(m_dir.filePath(name)); }
    [[nodiscard]] QDir dir(const QString& name) const
    {
        REQUIRE(QDir(m_dir.path()).mkpath(name));
        return QDir(m_dir.filePath(name));
    }

private:
    QTemporaryDir m_dir;
};

struct Setup {
    Setup() : profile(profileOf(kProfile))
    {
        rig.host.setChannel(phone.channel, rig.now());
        rig.host.setProfile(profile, rig.now());
        rig.host.setActive(true, rig.now());
    }

    Phone phone{7};
    Rig rig;
    std::shared_ptr<const input::Profile> profile;
};

struct Reading {
    Reading(Setup& setup, const std::filesystem::path& folder, InputGrant grant, const std::filesystem::path& store,
            QString extension = {}, std::vector<std::string> permissions = {})
        : runtime(LogicSource{.folder = folder,
                              .entry = "logic.ts",
                              .profile = setup.profile,
                              .id = QStringLiteral("reader"),
                              .name = QStringLiteral("Reader"),
                              .device = QStringLiteral("test phone"),
                              .deviceKey = QStringLiteral("test-phone"),
                              .extension = std::move(extension),
                              .permissions = std::move(permissions),
                              .inputGrant = std::move(grant)},
                  std::make_shared<ReadingLane>(setup.rig, setup.phone.channel), store)
    {
        QObject::connect(&runtime, &ScriptRuntime::loaded, [this] { ++loads; });
        QObject::connect(&runtime, &ScriptRuntime::inputWanted, [this] { ++asked; });
        QObject::connect(&runtime, &ScriptRuntime::reported,
                         [this](const LogicReport& report) { texts.append(report.text); });
        runtime.setDevice({.model = "Pixel test", .android = 15, .width = 1080, .height = 2400});
    }

    [[nodiscard]] qsizetype count(const QString& text) const { return texts.count(text); }

    int loads = 0;
    int asked = 0;
    QStringList texts;
    ScriptRuntime runtime;
};

}

TEST_CASE("SCR-019: the user's own logic of a profile nobody allowed is refused with GS-API-098 and asks once; once "
          "allowed it reads at once and the list keeps the profile")
{
    const Application app;
    const Data data;
    InputReadGrants grants(data.path(QStringLiteral("input-read.json")));
    const QDir logic = data.dir(QStringLiteral("copied"));
    write(logic.filePath(QStringLiteral("logic.ts")), kReadingLogic);
    Setup setup;
    Reading reader(setup, pathOf(logic.path()), profileGrant(QStringLiteral("copied")),
                   data.path(QStringLiteral("store")));

    REQUIRE(setup.rig.until([&] { return reader.count(QStringLiteral("load GS-API-098")) == 1; }));
    setup.rig.press(kSpace);
    REQUIRE(setup.rig.until([&] { return reader.count(QStringLiteral("press GS-API-098")) == 1; }));
    setup.rig.settle(100ms);
    CHECK(reader.asked == 1);
    CHECK_FALSE(grants.allows(profileGrant(QStringLiteral("copied"))));

    reader.runtime.allowInput();
    CHECK(grants.allows(profileGrant(QStringLiteral("copied"))));
    CHECK(grants.listed(InputReader::Profile) == QStringList{QStringLiteral("copied")});
    CHECK(listedIn(data.file(QStringLiteral("input-read.json")), QStringLiteral("profiles")) ==
          QStringList{QStringLiteral("copied")});
    setup.rig.press(kSpace);
    REQUIRE(setup.rig.until([&] { return reader.count(QStringLiteral("press 0.25 0.75 true")) == 1; }));
    CHECK(reader.asked == 1);
    CHECK(reader.loads == 1);
}

TEST_CASE("SCR-019: logic the user did not allow is not asked about again until it reloads, and logic of a run "
          "without a lasting name is allowed only for that run")
{
    const Application app;
    const Data data;
    InputReadGrants grants(data.path(QStringLiteral("input-read.json")));
    const QDir logic = data.dir(QStringLiteral("logic"));
    write(logic.filePath(QStringLiteral("logic.ts")), kReadingLogic);
    Setup setup;
    Reading reader(setup, pathOf(logic.path()), InputGrant{}, data.path(QStringLiteral("store")));

    REQUIRE(setup.rig.until([&] { return reader.count(QStringLiteral("load GS-API-098")) == 1; }));
    setup.rig.press(kSpace);
    REQUIRE(setup.rig.until([&] { return reader.count(QStringLiteral("press GS-API-098")) == 1; }));
    setup.rig.settle(100ms);
    CHECK(reader.asked == 1);

    reader.runtime.reload();
    REQUIRE(setup.rig.until([&] { return reader.count(QStringLiteral("load GS-API-098")) == 2; }));
    setup.rig.settle(100ms);
    CHECK(reader.asked == 2);

    reader.runtime.allowInput();
    CHECK(grants.listed(InputReader::Profile).isEmpty());
    CHECK(grants.listed(InputReader::Task).isEmpty());
    setup.rig.press(kSpace);
    REQUIRE(setup.rig.until([&] { return reader.count(QStringLiteral("press 0.25 0.75 true")) == 1; }));
    reader.runtime.reload();
    REQUIRE(setup.rig.until([&] { return reader.count(QStringLiteral("load 0.25 0.75 true")) == 1; }));
    CHECK(reader.asked == 2);
}

TEST_CASE("SCR-019: a profile whose logic is added in the logic view, and a task made in GameScrcpy, read input "
          "without being asked")
{
    const Application app;
    const Data data;
    const QDir profiles = data.dir(QStringLiteral("profiles"));
    write(profiles.filePath(QStringLiteral("mine.json")), kProfile);
    ProfileLibrary library(data.path(QStringLiteral("profiles")), deletingFolders());
    TaskLibrary tasks(data.path(QStringLiteral("tasks")));
    InputReadGrants grants(data.path(QStringLiteral("input-read.json")));
    grants.follow(library, tasks);
    CHECK(grants.listed(InputReader::Profile).isEmpty());

    const QString folder = library.ensureFolder(QStringLiteral("mine"));
    REQUIRE_FALSE(folder.isEmpty());
    CHECK_FALSE(grants.allows(profileGrant(QStringLiteral("mine"))));
    REQUIRE(writeStarterLogic(folder, {}) == LogicWrite::Written);
    CHECK(grants.allowFolder(folder));
    CHECK(grants.allows(profileGrant(QStringLiteral("mine"))));
    CHECK_FALSE(grants.allowFolder(data.file(QStringLiteral("elsewhere"))));

    write(QDir(folder).filePath(QStringLiteral("logic.ts")), kReadingLogic);
    Setup setup;
    Reading reader(setup, pathOf(folder), profileGrant(QStringLiteral("mine")), data.path(QStringLiteral("store")));
    REQUIRE(setup.rig.until([&] { return reader.count(QStringLiteral("load 0.25 0.75 true")) == 1; }));
    setup.rig.press(kSpace);
    REQUIRE(setup.rig.until([&] { return reader.count(QStringLiteral("press 0.25 0.75 true")) == 1; }));
    CHECK(reader.asked == 0);

    const QString daily = tasks.add(QStringLiteral("Daily"));
    REQUIRE_FALSE(daily.isEmpty());
    CHECK(grants.allows(taskGrant(daily)));
    write(data.file(QStringLiteral("tasks/copied/task.ts")), kTask);
    tasks.reload();
    REQUIRE(tasks.find(QStringLiteral("copied")) != nullptr);
    CHECK_FALSE(grants.allows(taskGrant(QStringLiteral("copied"))));
    CHECK(grants.allowFolder(data.file(QStringLiteral("tasks/copied"))));
    CHECK(grants.allows(taskGrant(QStringLiteral("copied"))));
    CHECK(listedIn(data.file(QStringLiteral("input-read.json")), QStringLiteral("tasks")) ==
          QStringList{QStringLiteral("copied"), daily});
}

TEST_CASE("SCR-019: a task turned from a recording reads input without being asked")
{
    const Application app;
    const Data data;
    ProfileLibrary library(data.path(QStringLiteral("profiles")), deletingFolders());
    TaskLibrary tasks(data.path(QStringLiteral("tasks")));
    InputReadGrants grants(data.path(QStringLiteral("input-read.json")));
    grants.follow(library, tasks);

    const QString code = QString::fromUtf8(kTask.data(), static_cast<qsizetype>(kTask.size()));
    const QString recorded = tasks.addWithCode(QStringLiteral("Recorded"), code, {});
    REQUIRE_FALSE(recorded.isEmpty());
    CHECK(grants.allows(taskGrant(recorded)));
    CHECK(listedIn(data.file(QStringLiteral("input-read.json")), QStringLiteral("tasks")) == QStringList{recorded});
}

TEST_CASE("SCR-019: profiles imported with confirmation are allowed, copies keep what the original had, and a "
          "removed profile is forgotten")
{
    const Application app;
    const Data data;
    const QDir outside = data.dir(QStringLiteral("outside"));
    makeProfile(outside, QStringLiteral("demo"), kReadingLogic);
    write(outside.filePath(QStringLiteral("keys.json")), kProfile);
    const QDir profiles = data.dir(QStringLiteral("profiles"));
    ProfileLibrary library(data.path(QStringLiteral("profiles")), deletingFolders());
    TaskLibrary tasks(data.path(QStringLiteral("tasks")));
    InputReadGrants grants(data.path(QStringLiteral("input-read.json")));
    grants.follow(library, tasks);

    const QUrl demo = QUrl::fromLocalFile(outside.filePath(QStringLiteral("demo")));
    const QVariantMap asked = library.importFiles({demo});
    CHECK(asked[QStringLiteral("logic")].toList().size() == 1);
    CHECK(grants.listed(InputReader::Profile).isEmpty());

    const QVariantMap confirmed = library.importFiles({demo}, true);
    CHECK(confirmed[QStringLiteral("imported")].toStringList().size() == 1);
    REQUIRE(library.find(QStringLiteral("demo")) != nullptr);
    CHECK(grants.allows(profileGrant(QStringLiteral("demo"))));
    const QVariantMap keys =
        library.importFiles({QUrl::fromLocalFile(outside.filePath(QStringLiteral("keys.json")))}, true);
    CHECK(keys[QStringLiteral("imported")].toStringList().size() == 1);
    CHECK_FALSE(grants.allows(profileGrant(QStringLiteral("keys"))));

    CHECK(library.duplicate(QStringLiteral("demo")) == QStringLiteral("demo-copy"));
    CHECK(grants.allows(profileGrant(QStringLiteral("demo-copy"))));

    makeProfile(profiles, QStringLiteral("copied"), kReadingLogic);
    library.reload();
    REQUIRE(library.find(QStringLiteral("copied")) != nullptr);
    CHECK_FALSE(grants.allows(profileGrant(QStringLiteral("copied"))));
    CHECK(library.duplicate(QStringLiteral("copied")) == QStringLiteral("copied-copy"));
    CHECK_FALSE(grants.allows(profileGrant(QStringLiteral("copied-copy"))));

    const QDir extension = data.dir(QStringLiteral("extension"));
    makeProfile(extension, QStringLiteral("standard"), kReadingLogic);
    const QByteArray json(kProfile.data(), static_cast<qsizetype>(kProfile.size()));
    library.setExtensionProfiles({ExtensionProfile{.id = QStringLiteral("alice.pack/standard"),
                                                   .extensionId = QStringLiteral("alice.pack"),
                                                   .extensionName = QStringLiteral("Alice pack"),
                                                   .version = QStringLiteral("1.0.0"),
                                                   .json = json,
                                                   .folder = pathOf(extension.filePath(QStringLiteral("standard")))}});
    CHECK_FALSE(grants.allows(profileGrant(QStringLiteral("alice.pack/standard"))));
    CHECK(library.duplicate(QStringLiteral("alice.pack/standard")) == QStringLiteral("standard-copy"));
    CHECK(grants.allows(profileGrant(QStringLiteral("standard-copy"))));

    CHECK(library.remove(QStringLiteral("demo")));
    CHECK_FALSE(grants.allows(profileGrant(QStringLiteral("demo"))));
    CHECK(grants.listed(InputReader::Profile) ==
          QStringList{QStringLiteral("demo-copy"), QStringLiteral("standard-copy")});
    CHECK(listedIn(data.file(QStringLiteral("input-read.json")), QStringLiteral("profiles")) ==
          QStringList{QStringLiteral("demo-copy"), QStringLiteral("standard-copy")});
}

TEST_CASE("SCR-019: the first start with the list allows the profiles with logic and the tasks already there, and "
          "nothing that comes later")
{
    const Application app;
    const Data data;
    const QDir profiles = data.dir(QStringLiteral("profiles"));
    makeProfile(profiles, QStringLiteral("logic-folder"), kReadingLogic);
    makeProfile(profiles, QStringLiteral("keys-folder"), {});
    write(profiles.filePath(QStringLiteral("keys.json")), kProfile);
    write(data.file(QStringLiteral("tasks/daily/task.ts")), kTask);
    const QString list = data.file(QStringLiteral("input-read.json"));
    {
        ProfileLibrary library(data.path(QStringLiteral("profiles")), deletingFolders());
        TaskLibrary tasks(data.path(QStringLiteral("tasks")));
        InputReadGrants grants(pathOf(list));
        CHECK(grants.fresh());
        CHECK_FALSE(QFileInfo::exists(list));
        grants.follow(library, tasks);
        CHECK_FALSE(grants.fresh());
        CHECK(grants.listed(InputReader::Profile) == QStringList{QStringLiteral("logic-folder")});
        CHECK(grants.listed(InputReader::Task) == QStringList{QStringLiteral("daily")});
        CHECK(listedIn(list, QStringLiteral("profiles")) == QStringList{QStringLiteral("logic-folder")});
        CHECK(listedIn(list, QStringLiteral("tasks")) == QStringList{QStringLiteral("daily")});
    }

    makeProfile(profiles, QStringLiteral("copied"), kReadingLogic);
    write(data.file(QStringLiteral("tasks/later/task.ts")), kTask);
    {
        ProfileLibrary library(data.path(QStringLiteral("profiles")), deletingFolders());
        TaskLibrary tasks(data.path(QStringLiteral("tasks")));
        InputReadGrants grants(pathOf(list));
        CHECK_FALSE(grants.fresh());
        grants.follow(library, tasks);
        CHECK(grants.listed(InputReader::Profile) == QStringList{QStringLiteral("logic-folder")});
        CHECK(grants.listed(InputReader::Task) == QStringList{QStringLiteral("daily")});
        CHECK_FALSE(grants.allows(profileGrant(QStringLiteral("copied"))));
        CHECK_FALSE(grants.allows(taskGrant(QStringLiteral("later"))));
    }
}

TEST_CASE("SCR-019: a damaged list is set aside and starts empty, and a list from another GameScrcpy is not saved "
          "over")
{
    const Application app;
    const Data data;
    const QString list = data.file(QStringLiteral("input-read.json"));
    write(list, "not a list");
    {
        InputReadGrants grants(pathOf(list));
        CHECK_FALSE(grants.fresh());
        CHECK_FALSE(grants.readOnly());
        CHECK(grants.listed(InputReader::Profile).isEmpty());
        CHECK(QFileInfo::exists(list + QStringLiteral(".bad")));
        grants.allow(profileGrant(QStringLiteral("pubg")));
        CHECK(listedIn(list, QStringLiteral("profiles")) == QStringList{QStringLiteral("pubg")});
    }

    constexpr std::string_view later = R"({"format":"gamescrcpy.inputRead","version":2,"profiles":["moba"]})";
    write(list, later);
    {
        InputReadGrants grants(pathOf(list));
        CHECK(grants.readOnly());
        CHECK_FALSE(grants.allows(profileGrant(QStringLiteral("moba"))));
        grants.allow(profileGrant(QStringLiteral("pubg")));
        CHECK(grants.allows(profileGrant(QStringLiteral("pubg"))));
        grants.allow(InputGrant{.reader = InputReader::Run, .id = QStringLiteral("run")});
        CHECK_FALSE(grants.allows(InputGrant{.reader = InputReader::Run, .id = QStringLiteral("run")}));
    }
    QFile kept(list);
    REQUIRE(kept.open(QIODevice::ReadOnly));
    CHECK(kept.readAll() == QByteArray(later.data(), static_cast<qsizetype>(later.size())));
}

TEST_CASE("SCR-019: an extension's logic is not asked; the permission input.read decides as before")
{
    const Application app;
    const Data data;
    InputReadGrants grants(data.path(QStringLiteral("input-read.json")));
    const QDir logic = data.dir(QStringLiteral("extension"));
    write(logic.filePath(QStringLiteral("logic.ts")), kLoadReadingLogic);
    Setup setup;
    {
        Reading allowed(setup, pathOf(logic.path()), InputGrant{}, data.path(QStringLiteral("store")),
                        QStringLiteral("alice.helper"), {"input.read"});
        CHECK(allowed.runtime.sandboxed());
        REQUIRE(setup.rig.until([&] { return allowed.count(QStringLiteral("load 0.25 0.75 true")) == 1; }));
        setup.rig.settle(100ms);
        CHECK(allowed.asked == 0);
    }
    {
        Reading denied(setup, pathOf(logic.path()), InputGrant{}, data.path(QStringLiteral("store")),
                       QStringLiteral("alice.helper"), {});
        REQUIRE(setup.rig.until([&] { return denied.count(QStringLiteral("load GS-API-072")) == 1; }));
        setup.rig.settle(100ms);
        CHECK(denied.asked == 0);
    }
    CHECK(grants.listed(InputReader::Profile).isEmpty());
}
