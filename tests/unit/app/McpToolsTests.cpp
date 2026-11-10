#include "ComponentLibrary.h"
#include "McpDrafts.h"
#include "McpTestSupport.h"
#include "McpTools.h"
#include "ProfileEditor.h"
#include "ProfileLibrary.h"
#include "ScriptRuntime.h"
#include "TaskLibrary.h"

#include <gs/ext/Manifest.h>
#include <gs/ext/Package.h>
#include <gs/input/Profile.h>
#include <gs/media/FrameSnapshot.h>
#include <gs/script/ApiFiles.h>
#include <gs/script/Resources.h>

#include <doctest/doctest.h>

#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using gs::app::ComponentLibrary;
using gs::app::McpAnswer;
using gs::app::McpCall;
using gs::app::McpConfirmation;
using gs::app::McpDevices;
using gs::app::McpDraftFile;
using gs::app::McpDraftPreview;
using gs::app::McpDrafts;
using gs::app::McpFrameCapture;
using gs::app::McpOutcome;
using gs::app::McpPhone;
using gs::app::McpTools;
using gs::app::ProfileEditor;
using gs::app::ProfileLibrary;
using gs::app::ScriptJob;
using gs::app::TaskLibrary;

namespace {

namespace fs = std::filesystem;

const QString kClient = QStringLiteral("Test client");

class FakeDevices
{
public:
    McpDevices hooks()
    {
        McpDevices made;
        made.phones = [this] { return phones; };
        made.screenshot = [this](const QString&, std::function<void(const QImage&)> done) {
            if (!canShoot) {
                return false;
            }
            if (shot) {
                done(*shot);
            } else {
                waitingShot = std::move(done);
            }
            return true;
        };
        made.frames = [this](const QString&) -> McpFrameCapture {
            if (frame == nullptr) {
                return {};
            }
            return [picture = frame] { return picture; };
        };
        made.startScript = [this](const QString& serial, ScriptJob job, quint64& number) {
            startedOn = serial;
            jobs.push_back(std::move(job));
            number = ++lastRun;
            return refusal;
        };
        made.stopScript = [this](quint64 number) {
            stopped.push_back(number);
            if (!jobs.empty() && jobs.back().ended) {
                jobs.back().ended(QStringLiteral("stopped"), {});
            }
        };
        made.recording = [this](const QString&) { return recordingNow; };
        made.startRecording = [this](const QString& serial) {
            recordingNow = true;
            recordedOn = serial;
            return QString();
        };
        made.stopRecording = [this](const QString&) -> QVariantMap {
            if (!recordingNow) {
                return {};
            }
            recordingNow = false;
            return {{QStringLiteral("name"), QStringLiteral("Demo phone 2026-11-06 10.00.00.gsrec")},
                    {QStringLiteral("folder"), QStringLiteral("C:\\recordings\\Demo phone 2026-11-06 10.00.00.gsrec")},
                    {QStringLiteral("duration"), QStringLiteral("00:05")},
                    {QStringLiteral("video"), true}};
        };
        made.recordingsFolder = [this] { return recordings; };
        made.openEditor = [this](const QString& serial, const QString&, QString& problem) -> ProfileEditor* {
            if (editor == nullptr) {
                problem = QStringLiteral("no editor in this test");
                return nullptr;
            }
            editorOn = serial;
            return editor;
        };
        made.materials = [this](const gs::app::McpMaterialsRequest& request, gs::app::McpMaterialsDone done) {
            materialsAsked.push_back(request);
            if (materialsWrites && !request.folder.empty()) {
                std::error_code ignored;
                fs::create_directories(request.folder / "res", ignored);
                gs::script::Resources resources;
                resources.points.push_back({.name = "fire", .at = {0.5, 0.5}});
                static_cast<void>(gs::script::saveResources(request.folder / "res" / "resources.json", resources));
            }
            if (materialsNow) {
                done(materialsResults);
            } else {
                materialsWaiting = std::move(done);
            }
            return materialsRefusal;
        };
        made.endMaterials = [this](const QString& client) {
            materialsEnded.append(client);
            if (materialsWaiting) {
                const gs::app::McpMaterialsDone done = std::move(materialsWaiting);
                materialsWaiting = nullptr;
                done({});
            }
        };
        made.openDraftPreview = [this](const QString& target, const QString& serial, QString& problem) {
            previewed = target;
            previewedOn = serial;
            if (!previewOpens) {
                problem = QStringLiteral("no mirroring phone in this test");
                return false;
            }
            return true;
        };
        return made;
    }

    std::vector<McpPhone> phones;
    bool canShoot = false;
    std::optional<QImage> shot;
    std::function<void(const QImage&)> waitingShot;
    std::shared_ptr<const gs::media::LumaSnapshot> frame;
    std::vector<ScriptJob> jobs;
    std::vector<quint64> stopped;
    QString refusal;
    QString startedOn;
    quint64 lastRun = 0;
    bool recordingNow = false;
    QString recordedOn;
    QString recordings;
    ProfileEditor* editor = nullptr;
    QString editorOn;
    std::vector<gs::app::McpMaterialsRequest> materialsAsked;
    gs::app::McpMaterialsDone materialsWaiting;
    std::vector<gs::app::McpMaterialResult> materialsResults;
    QString materialsRefusal;
    QStringList materialsEnded;
    bool materialsNow = true;
    bool materialsWrites = false;
    QString previewed;
    QString previewedOn;
    bool previewOpens = true;
};

struct Confirmer {
    std::optional<McpAnswer> answer;
    std::vector<McpConfirmation> shown;
    std::function<void(McpAnswer)> pending;

    void asked(McpConfirmation confirmation, std::function<void(McpAnswer)> done)
    {
        shown.push_back(std::move(confirmation));
        if (answer) {
            done(*answer);
        } else {
            pending = std::move(done);
        }
    }
};

struct Reply {
    bool done = false;
    McpOutcome outcome;
};

QString textOf(const McpOutcome& outcome, qsizetype index = 0)
{
    return outcome.content.at(index).toObject().value(QStringLiteral("text")).toString();
}

QJsonObject objectOf(const McpOutcome& outcome, qsizetype index = 0)
{
    return QJsonDocument::fromJson(textOf(outcome, index).toUtf8()).object();
}

QString scriptData(const QString& relative)
{
    return QString::fromUtf8(GS_SCRIPT_TEST_DATA_DIR) + u'/' + relative;
}

void writeFile(const fs::path& path, const std::string& contents)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << contents;
}

std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

fs::path pathOf(const QString& text)
{
    return {text.toStdWString()};
}

std::shared_ptr<gs::media::LumaSnapshot> loadFrame(const char* name)
{
    QFile file(QString::fromUtf8(GS_VISION_TEST_DATA_DIR) + u'/' + QString::fromLatin1(name));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    qsizetype at = 0;
    const auto token = [&bytes, &at] {
        while (at < bytes.size() && std::isspace(static_cast<unsigned char>(bytes[at])) != 0) {
            ++at;
        }
        const qsizetype start = at;
        while (at < bytes.size() && std::isspace(static_cast<unsigned char>(bytes[at])) == 0) {
            ++at;
        }
        return bytes.mid(start, at - start);
    };
    REQUIRE(token() == "P5");
    const int width = token().toInt();
    const int height = token().toInt();
    REQUIRE(token() == "255");
    ++at;
    auto shot = std::make_shared<gs::media::LumaSnapshot>();
    shot->width = width;
    shot->height = height;
    const auto size = static_cast<qsizetype>(width) * height;
    REQUIRE(bytes.size() >= at + size);
    shot->luma.assign(bytes.begin() + at, bytes.begin() + at + size);
    return shot;
}

std::shared_ptr<gs::media::LumaSnapshot> colored(std::shared_ptr<gs::media::LumaSnapshot> shot)
{
    shot->chroma.assign(static_cast<std::size_t>(2 * shot->chromaWidth() * shot->chromaHeight()), 128);
    shot->matrix.rows = {{{1.0F, 0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F, 0.0F}}};
    return shot;
}

QByteArray pngOf(const QImage& picture)
{
    QByteArray png;
    QBuffer buffer(&png);
    REQUIRE(buffer.open(QIODevice::WriteOnly));
    REQUIRE(picture.save(&buffer, "PNG"));
    return png;
}

QImage cropOf(const gs::media::LumaSnapshot& shot, int x, int y, int width, int height)
{
    QImage picture(width, height, QImage::Format_Grayscale8);
    for (int row = 0; row < height; ++row) {
        std::memcpy(picture.scanLine(row),
                    shot.luma.data() + static_cast<std::size_t>(y + row) * static_cast<std::size_t>(shot.width) + x,
                    static_cast<std::size_t>(width));
    }
    return picture;
}

const McpDraftFile* fileIn(const std::vector<McpDraftFile>& files, const QString& path)
{
    for (const McpDraftFile& file : files) {
        if (file.path == path) {
            return &file;
        }
    }
    return nullptr;
}

constexpr const char* kProfile = R"({"format":"gamescrcpy.profile","version":3,"meta":{"name":"Logic demo"},
"controls":[{"id":"jump","name":"Jump","type":"hold","anchor":"bottomRight","offset":[-0.2,-0.3],
"inputs":{"press":"Space"}}],"vars":[]})";

struct Setup {
    gs::test::McpApplication application;
    gs::test::McpFolder folder;
    ProfileLibrary library{folder.path() / "profiles"};
    ComponentLibrary components{folder.path() / "examples", folder.path() / "components"};
    TaskLibrary tasks{folder.path() / "tasks"};
    McpDrafts drafts{{.root = folder.path() / "drafts", .profiles = &library, .components = &components, .tasks = &tasks}};
    FakeDevices devices;
    gs::ManualClock clock;
    Confirmer confirmer;
    McpTools tools{{.profiles = &library,
                    .logFile = folder.path() / "logs" / "gamescrcpy.log",
                    .devices = devices.hooks(),
                    .components = &components,
                    .tasks = &tasks,
                    .drafts = &drafts,
                    .program = pathOf(QString::fromUtf8(GS_CLI_PROGRAM))},
                   clock};
    quint64 lastCall = 0;

    std::shared_ptr<Reply> start(const QString& tool, const QJsonObject& arguments = {})
    {
        auto reply = std::make_shared<Reply>();
        tools.call(McpCall{.id = ++lastCall,
                           .client = kClient,
                           .tool = tool,
                           .arguments = arguments,
                           .confirm =
                               [this](McpConfirmation shown, std::function<void(McpAnswer)> done) {
                                   confirmer.asked(std::move(shown), std::move(done));
                               }},
                   [reply](McpOutcome outcome) {
                       reply->done = true;
                       reply->outcome = std::move(outcome);
                   });
        return reply;
    }

    McpOutcome run(const QString& tool, const QJsonObject& arguments = {})
    {
        const std::shared_ptr<Reply> reply = start(tool, arguments);
        REQUIRE(gs::test::waitUntil([&reply] { return reply->done; }));
        return reply->outcome;
    }

    fs::path profileFolder(const std::string& id)
    {
        const fs::path made = folder.path() / "profiles" / id;
        writeFile(made / "profile.json", kProfile);
        writeFile(made / "logic.ts", "controls.jump.onPress(() => {\n    log.info(\"jump\");\n});\n");
        library.reload();
        return made;
    }

    void phone(const fs::path& profile = {})
    {
        devices.phones = {McpPhone{.serial = QStringLiteral("demo"),
                                   .name = QStringLiteral("Demo phone"),
                                   .width = 1080,
                                   .height = 2400,
                                   .app = QStringLiteral("com.example.game"),
                                   .profileId = QStringLiteral("logic"),
                                   .profileName = QStringLiteral("Logic demo"),
                                   .folder = profile}};
    }
};

}

TEST_CASE("the MCP tools list names every tool in a fixed order, each with a closed object schema")
{
    Setup setup;
    const QJsonArray listed = setup.tools.tools();
    QStringList names;
    for (qsizetype index = 0; index < listed.size(); ++index) {
        const QJsonObject tool = listed.at(index).toObject();
        const QString name = tool.value(QStringLiteral("name")).toString();
        CAPTURE(name.toStdString());
        names.append(name);
        CHECK_FALSE(tool.value(QStringLiteral("description")).toString().isEmpty());
        const QJsonObject schema = tool.value(QStringLiteral("inputSchema")).toObject();
        CHECK(schema.value(QStringLiteral("type")).toString() == u"object");
        CHECK(schema.value(QStringLiteral("additionalProperties")).toBool(true) == false);
        const QJsonObject properties = schema.value(QStringLiteral("properties")).toObject();
        for (const QJsonValue required : schema.value(QStringLiteral("required")).toArray()) {
            CHECK(properties.contains(required.toString()));
        }
    }
    CHECK(names == QStringList{QStringLiteral("api_reference"),     QStringLiteral("run_replay"),
                               QStringLiteral("list_phones"),       QStringLiteral("get_frame"),
                               QStringLiteral("find_image"),        QStringLiteral("read_logs"),
                               QStringLiteral("list_profiles"),     QStringLiteral("read_profile"),
                               QStringLiteral("write_profile"),     QStringLiteral("run_script"),
                               QStringLiteral("list_components"),   QStringLiteral("read_component"),
                               QStringLiteral("write_component"),   QStringLiteral("list_tasks"),
                               QStringLiteral("read_task"),         QStringLiteral("write_task"),
                               QStringLiteral("read_draft"),        QStringLiteral("draft_diff"),
                               QStringLiteral("apply_draft"),       QStringLiteral("discard_draft"),
                               QStringLiteral("open_draft_preview"),
                               QStringLiteral("read_blueprint"),    QStringLiteral("edit_blueprint"),
                               QStringLiteral("check"),             QStringLiteral("start_recording"),
                               QStringLiteral("stop_recording"),    QStringLiteral("list_recordings"),
                               QStringLiteral("propose_keymap"),    QStringLiteral("keymap_draft_status"),
                               QStringLiteral("request_materials"),
                               QStringLiteral("create_extension"),  QStringLiteral("write_extension"),
                               QStringLiteral("validate_extension"), QStringLiteral("pack_extension")});
    const McpOutcome wrong = setup.run(QStringLiteral("list_phones"), {{QStringLiteral("serial"), 3}});
    CHECK(wrong.failed);
    CHECK(wrong.code == u"GS-MCP-004");
}

TEST_CASE("api_reference looks up a function, a namespace, words and the profile format")
{
    Setup setup;
    const QString index = textOf(setup.run(QStringLiteral("api_reference")));
    CHECK(index.contains(QStringLiteral("# GameScrcpy script API")));
    CHECK(index.contains(QStringLiteral("profile format")));

    const QString find = textOf(setup.run(QStringLiteral("api_reference"), {{QStringLiteral("query"), QStringLiteral("vision.find")}}));
    CHECK(find.startsWith(QStringLiteral("## vision.find(")));
    CHECK_FALSE(find.contains(QStringLiteral("## vision.findAll(")));

    const QString store = textOf(setup.run(QStringLiteral("api_reference"), {{QStringLiteral("query"), QStringLiteral("Store")}}));
    CHECK(store.startsWith(QStringLiteral("# store")));
    CHECK(store.contains(QStringLiteral("## store.keys()")));

    const QString words = textOf(setup.run(QStringLiteral("api_reference"), {{QStringLiteral("query"), QStringLiteral("swipe duration")}}));
    CHECK(words.contains(QStringLiteral("touch.swipe")));

    const QString format = textOf(setup.run(QStringLiteral("api_reference"), {{QStringLiteral("query"), QStringLiteral("profile format")}}));
    CHECK(format.startsWith(QStringLiteral("# Profile format")));
    CHECK(format.contains(QStringLiteral("| `wasd` | `up`, `down`, `left`, `right`, `walk`, `sprint` |")));

    const McpOutcome nothing = setup.run(QStringLiteral("api_reference"), {{QStringLiteral("query"), QStringLiteral("zzqx")}});
    CHECK_FALSE(nothing.failed);
    CHECK(textOf(nothing).startsWith(QStringLiteral("Nothing in the API reference matches")));
}

TEST_CASE("the profile format shows the gamepad key for passthrough, and its example loads as a profile (ADR-0103)")
{
    Setup setup;
    const QString format = textOf(setup.run(QStringLiteral("api_reference"), {{QStringLiteral("query"), QStringLiteral("profile format")}}));
    CHECK(format.contains(QStringLiteral("  \"gamepad\": \"passthrough\",\n")));
    CHECK(format.contains(QStringLiteral("- `gamepad`: `passthrough` ")));
    CHECK(format.contains(QStringLiteral("`map`")));
    const qsizetype begin = format.indexOf(QStringLiteral("```json\n"));
    REQUIRE(begin >= 0);
    const qsizetype end = format.indexOf(QStringLiteral("```"), begin + 8);
    REQUIRE(end > begin);
    const std::string example = format.mid(begin + 8, end - begin - 8).toStdString();
    const gs::input::LoadResult loaded = gs::input::loadProfile(example);
    for (const gs::input::Diagnostic& diagnostic : loaded.diagnostics) {
        CAPTURE(diagnostic.path);
        CAPTURE(diagnostic.detail);
        CHECK(diagnostic.severity != gs::input::Severity::Error);
    }
    REQUIRE(loaded.ok());
    CHECK(loaded.profile.gamepad == gs::input::GamepadMode::Passthrough);
}

TEST_CASE("run_replay plays a profile folder against a recorded session as gs run --replay does, never a phone")
{
    Setup setup;
    const QString demo = scriptData(QStringLiteral("profile-demo"));
    const McpOutcome played = setup.run(QStringLiteral("run_replay"),
                                        {{QStringLiteral("profile"), demo},
                                         {QStringLiteral("session"), demo + QStringLiteral("/sessions/basic.json")}});
    CAPTURE(textOf(played).toStdString());
    CHECK_FALSE(played.failed);
    CHECK(textOf(played).contains(QStringLiteral("\n160 control fire up\n")));
    CHECK(textOf(played).endsWith(QStringLiteral("ended at 700 ms")));
    CHECK(played.content.size() == 1);
    CHECK(setup.devices.jobs.empty());

    const fs::path recording = setup.folder.path() / "recording";
    writeFile(recording / "session.json", readFile(fs::path(demo.toStdWString()) / "sessions" / "basic.json"));
    const McpOutcome fromFolder =
        setup.run(QStringLiteral("run_replay"),
                  {{QStringLiteral("profile"), demo},
                   {QStringLiteral("session"), QString::fromStdWString(recording.wstring())},
                   {QStringLiteral("untilMs"), 200}});
    CHECK_FALSE(fromFolder.failed);
    CHECK(textOf(fromFolder).contains(QStringLiteral("160 control fire up")));
    CHECK(textOf(fromFolder).endsWith(QStringLiteral("ended at 200 ms (time limit)")));

    const McpOutcome missing = setup.run(QStringLiteral("run_replay"),
                                         {{QStringLiteral("profile"), demo},
                                          {QStringLiteral("session"), QString::fromStdWString(setup.folder.path().wstring())}});
    CHECK(missing.failed);
    CHECK(missing.code == u"GS-MCP-011");
}

TEST_CASE("run_replay runs code given inline from a temporary folder beside the profile's controls")
{
    Setup setup;
    const QString demo = scriptData(QStringLiteral("profile-demo"));
    const QString session = demo + QStringLiteral("/sessions/basic.json");
    const McpOutcome given = setup.run(
        QStringLiteral("run_replay"),
        {{QStringLiteral("profile"), demo},
         {QStringLiteral("session"), session},
         {QStringLiteral("code"),
          QStringLiteral("log.info(\"hi\");\nexport async function start(): Promise<void> {\n    log.info(\"started\");\n}\n")}});
    CAPTURE(textOf(given).toStdString());
    CHECK_FALSE(given.failed);
    CHECK(textOf(given).contains(QStringLiteral("# log info hi")));
    CHECK(textOf(given).contains(QStringLiteral("# log info started")));

    const McpOutcome broken = setup.run(QStringLiteral("run_replay"),
                                        {{QStringLiteral("session"), session},
                                         {QStringLiteral("code"), QStringLiteral("throw new Error(\"boom\");\n")}});
    CHECK(broken.failed);
    CHECK(broken.code == u"GS-MCP-013");
    CHECK(textOf(broken).contains(QStringLiteral("boom")));
}

TEST_CASE("run_replay with draft runs this client's draft, and trace returns every step")
{
    Setup setup;
    const QString demo = scriptData(QStringLiteral("profile-demo"));
    const fs::path made = setup.folder.path() / "profiles" / "demo";
    writeFile(made / "profile.json", readFile(pathOf(demo) / "profile.json"));
    writeFile(made / "logic.ts", readFile(pathOf(demo) / "logic.ts"));
    setup.library.reload();
    const QString session = demo + QStringLiteral("/sessions/basic.json");
    CHECK(setup.run(QStringLiteral("run_replay"), {{QStringLiteral("profile"), QStringLiteral("demo")},
                                                   {QStringLiteral("session"), session},
                                                   {QStringLiteral("draft"), true}})
              .code == u"GS-MCP-014");

    const McpOutcome written = setup.run(
        QStringLiteral("write_profile"),
        {{QStringLiteral("profile"), QStringLiteral("demo")},
         {QStringLiteral("files"),
          QJsonObject{{QStringLiteral("logic.ts"),
                       QStringLiteral("controls.jump.onPress(async () => {\n    log.info(\"drafted jump\");\n});\n"
                                      "export function start(): void {}\n")}}}});
    REQUIRE_FALSE(written.failed);

    const McpOutcome real = setup.run(QStringLiteral("run_replay"),
                                      {{QStringLiteral("profile"), QStringLiteral("demo")}, {QStringLiteral("session"), session}});
    CAPTURE(textOf(real).toStdString());
    CHECK(textOf(real).contains(QStringLiteral("control fire")));
    CHECK_FALSE(textOf(real).contains(QStringLiteral("drafted jump")));

    const McpOutcome drafted = setup.run(QStringLiteral("run_replay"), {{QStringLiteral("profile"), QStringLiteral("demo")},
                                                                         {QStringLiteral("session"), session},
                                                                         {QStringLiteral("draft"), true},
                                                                         {QStringLiteral("trace"), true}});
    CAPTURE(textOf(drafted).toStdString());
    REQUIRE_FALSE(drafted.failed);
    CHECK(textOf(drafted).contains(QStringLiteral("# log info drafted jump")));
    CHECK_FALSE(textOf(drafted).contains(QStringLiteral("control fire")));
    REQUIRE(drafted.content.size() >= 2);
    const QJsonArray steps = objectOf(drafted, 1).value(QStringLiteral("steps")).toArray();
    bool code = false;
    bool logged = false;
    for (const QJsonValue& value : steps) {
        const QJsonObject step = value.toObject();
        code = code || (step.value(QStringLiteral("kind")).toString() == u"code" &&
                        step.value(QStringLiteral("file")).toString() == u"logic.ts");
        logged = logged || (step.value(QStringLiteral("kind")).toString() == u"log" &&
                            step.value(QStringLiteral("text")).toString().contains(QStringLiteral("drafted jump")));
    }
    CHECK(code);
    CHECK(logged);
}

TEST_CASE("read_profile returns a profile folder's files and resources")
{
    Setup setup;
    const fs::path made = setup.profileFolder("shooter");
    writeFile(made / "res" / "coin.png", std::string(pngOf(QImage(4, 4, QImage::Format_RGB32)).toStdString()));
    REQUIRE_FALSE(static_cast<bool>(gs::script::saveResources(
        made / "res" / "resources.json",
        gs::script::Resources{.templates = {gs::script::TemplateResource{.name = "coin", .file = "coin.png"}}})));
    setup.library.reload();
    const McpOutcome read = setup.run(QStringLiteral("read_profile"), {{QStringLiteral("profile"), QStringLiteral("shooter")}});
    REQUIRE_FALSE(read.failed);
    const QJsonObject result = QJsonDocument::fromJson(textOf(read).toUtf8()).object();
    CHECK(result.value(QStringLiteral("logic")).toString() == u"logic.ts");
    const QJsonObject files = result.value(QStringLiteral("files")).toObject();
    CHECK(files.value(QStringLiteral("profile.json")).toString().contains(QStringLiteral("gamescrcpy.profile")));
    CHECK(files.value(QStringLiteral("logic.ts")).toString().contains(QStringLiteral("controls.jump")));
    CHECK(files.contains(QStringLiteral("res/resources.json")));
    CHECK(result.value(QStringLiteral("otherFiles")).toArray().contains(QStringLiteral("res/coin.png")));
    CHECK(result.value(QStringLiteral("resources")).toObject().value(QStringLiteral("templates")).toArray() ==
          QJsonArray{QStringLiteral("coin")});

    const McpOutcome unknown = setup.run(QStringLiteral("read_profile"), {{QStringLiteral("profile"), QStringLiteral("nope")}});
    CHECK(unknown.code == u"GS-MCP-006");
}

TEST_CASE("write_profile writes into the draft without asking; apply_draft asks once and lists every file's lines")
{
    Setup setup;
    const fs::path made = setup.profileFolder("shooter");
    const std::string before = readFile(made / "logic.ts");

    const McpOutcome invalid = setup.run(
        QStringLiteral("write_profile"),
        {{QStringLiteral("profile"), QStringLiteral("shooter")},
         {QStringLiteral("files"),
          QJsonObject{{QStringLiteral("profile.json"), QStringLiteral("{\"format\":\"gamescrcpy.profile\",\"version\":3,"
                                                                      "\"controls\":[{\"id\":\"x\",\"type\":\"nonsense\"}]}")}}}});
    CHECK(invalid.failed);
    CHECK(invalid.code == u"GS-CHECK-005");
    CHECK(textOf(invalid).contains(QStringLiteral("profile.json: error GS-CHECK-005 $.controls[0].type")));

    for (const QString& path : {QStringLiteral("../escape.ts"), QStringLiteral("types/x.d.ts"), QStringLiteral("res/a.png"),
                                QStringLiteral("C:/x.ts"), QStringLiteral(".hidden.ts"), QStringLiteral("logic.blueprint.json")}) {
        const McpOutcome refused = setup.run(QStringLiteral("write_profile"),
                                             {{QStringLiteral("profile"), QStringLiteral("shooter")},
                                              {QStringLiteral("files"), QJsonObject{{path, QStringLiteral("x")}}}});
        CAPTURE(path.toStdString());
        CHECK(refused.code == u"GS-MCP-004");
    }
    CHECK(setup.drafts.targets(kClient).isEmpty());

    const QJsonObject change{{QStringLiteral("profile"), QStringLiteral("shooter")},
                             {QStringLiteral("files"),
                              QJsonObject{{QStringLiteral("logic.ts"),
                                           QStringLiteral("controls.jump.onPress(() => {\n    log.info(\"jump!\");\n});\n"
                                                          "log.info(\"ready\");\n")},
                                          {QStringLiteral("helper.ts"), QStringLiteral("export const two = 2;\n")}}}};
    const McpOutcome written = setup.run(QStringLiteral("write_profile"), change);
    CAPTURE(textOf(written).toStdString());
    REQUIRE_FALSE(written.failed);
    CHECK(textOf(written).contains(QStringLiteral("logic.ts (+2 -1)")));
    CHECK(setup.confirmer.shown.empty());
    CHECK(readFile(made / "logic.ts") == before);
    CHECK_FALSE(fs::exists(made / "helper.ts"));
    CHECK(setup.drafts.targets(kClient) == QStringList{QStringLiteral("profile:shooter")});
    CHECK(textOf(setup.run(QStringLiteral("write_profile"), change)).startsWith(QStringLiteral("Nothing changed")));

    const QJsonObject draft = objectOf(setup.run(QStringLiteral("read_profile"), {{QStringLiteral("profile"), QStringLiteral("shooter")},
                                                                                  {QStringLiteral("draft"), true}}));
    CHECK(draft.value(QStringLiteral("draft")).toBool());
    CHECK(draft.value(QStringLiteral("files")).toObject().value(QStringLiteral("logic.ts")).toString().contains(QStringLiteral("jump!")));
    CHECK(draft.value(QStringLiteral("files")).toObject().contains(QStringLiteral("helper.ts")));

    const QJsonObject diff = objectOf(setup.run(QStringLiteral("draft_diff"), {{QStringLiteral("target"), QStringLiteral("profile:shooter")}}));
    const QJsonArray diffFiles = diff.value(QStringLiteral("files")).toArray();
    REQUIRE(diffFiles.size() == 2);
    CHECK(diffFiles.at(0).toObject().value(QStringLiteral("path")).toString() == u"helper.ts");
    CHECK(diffFiles.at(0).toObject().value(QStringLiteral("state")).toString() == u"added");
    CHECK(diffFiles.at(1).toObject().value(QStringLiteral("state")).toString() == u"changed");
    CHECK(diff.value(QStringLiteral("diff")).toString().contains(QStringLiteral("-    log.info(\"jump\");\n+    log.info(\"jump!\");\n")));

    setup.confirmer.answer = McpAnswer::Refused;
    const McpOutcome refused = setup.run(QStringLiteral("apply_draft"), {{QStringLiteral("target"), QStringLiteral("profile:shooter")}});
    CHECK(refused.code == u"GS-MCP-008");
    REQUIRE(setup.confirmer.shown.size() == 1);
    const McpConfirmation& shown = setup.confirmer.shown.front();
    CHECK(shown.kind == u"apply");
    CHECK(shown.details.value(QStringLiteral("name")).toString() == u"Logic demo");
    const QVariantList files = shown.details.value(QStringLiteral("files")).toList();
    REQUIRE(files.size() == 2);
    const QVariantMap helper = files.at(0).toMap();
    const QVariantMap logic = files.at(1).toMap();
    CHECK(helper.value(QStringLiteral("path")).toString() == u"helper.ts");
    CHECK(helper.value(QStringLiteral("created")).toBool());
    CHECK(helper.value(QStringLiteral("added")).toInt() == 1);
    CHECK(logic.value(QStringLiteral("path")).toString() == u"logic.ts");
    CHECK(logic.value(QStringLiteral("added")).toInt() == 2);
    CHECK(logic.value(QStringLiteral("removed")).toInt() == 1);
    CHECK_FALSE(logic.value(QStringLiteral("conflict")).toBool());
    CHECK(logic.value(QStringLiteral("diff")).toString().contains(
        QStringLiteral("-    log.info(\"jump\");\n+    log.info(\"jump!\");\n")));
    CHECK(helper.value(QStringLiteral("diff")).toString().startsWith(QStringLiteral("--- /dev/null\n+++ b/helper.ts\n")));
    CHECK(shown.details.value(QStringLiteral("previewable")).toBool());
    CHECK(shown.details.value(QStringLiteral("target")).toString() == u"profile:shooter");
    CHECK(readFile(made / "logic.ts") == before);
    CHECK(setup.drafts.has(kClient, QStringLiteral("profile:shooter")));

    setup.confirmer.answer = McpAnswer::Unanswered;
    CHECK(setup.run(QStringLiteral("apply_draft"), {{QStringLiteral("target"), QStringLiteral("profile:shooter")}}).code ==
          u"GS-MCP-009");
    CHECK_FALSE(fs::exists(made / "helper.ts"));

    setup.confirmer.answer = McpAnswer::Allowed;
    const McpOutcome applied = setup.run(QStringLiteral("apply_draft"), {{QStringLiteral("target"), QStringLiteral("profile:shooter")}});
    CAPTURE(textOf(applied).toStdString());
    CHECK_FALSE(applied.failed);
    CHECK(textOf(applied).contains(QStringLiteral("logic.ts (+2 -1)")));
    CHECK(setup.confirmer.shown.size() == 3);
    CHECK(readFile(made / "logic.ts") == "controls.jump.onPress(() => {\n    log.info(\"jump!\");\n});\nlog.info(\"ready\");\n");
    CHECK(readFile(made / "helper.ts") == "export const two = 2;\n");
    CHECK(setup.drafts.targets(kClient).isEmpty());
    CHECK(setup.run(QStringLiteral("apply_draft"), {{QStringLiteral("target"), QStringLiteral("profile:shooter")}}).code ==
          u"GS-MCP-014");
}

TEST_CASE("a draft marks files changed in GameScrcpy after it was made, lists them first, and can be thrown away")
{
    Setup setup;
    const fs::path made = setup.profileFolder("shooter");
    REQUIRE_FALSE(setup.run(QStringLiteral("write_profile"),
                            {{QStringLiteral("profile"), QStringLiteral("shooter")},
                             {QStringLiteral("files"), QJsonObject{{QStringLiteral("logic.ts"), QStringLiteral("log.info(\"draft\");\n")},
                                                                   {QStringLiteral("notes.md"), QStringLiteral("plan\n")}}}})
                      .failed);
    writeFile(made / "logic.ts", "log.info(\"changed by the user\");\n");
    const std::vector<McpDraftFile> files = setup.drafts.diff(kClient, QStringLiteral("profile:shooter"));
    const McpDraftFile* logic = fileIn(files, QStringLiteral("logic.ts"));
    const McpDraftFile* notes = fileIn(files, QStringLiteral("notes.md"));
    REQUIRE(logic != nullptr);
    REQUIRE(notes != nullptr);
    CHECK(logic->state == u"changed");
    CHECK(logic->conflict);
    CHECK(notes->state == u"added");
    CHECK_FALSE(notes->conflict);
    CHECK(setup.drafts.fileDiff(kClient, QStringLiteral("profile:shooter"), QStringLiteral("logic.ts")) ==
          QStringLiteral("--- a/logic.ts\n+++ b/logic.ts\n@@ -1 +1 @@\n-log.info(\"changed by the user\");\n+log.info(\"draft\");\n"));

    setup.confirmer.answer = McpAnswer::Refused;
    CHECK(setup.run(QStringLiteral("apply_draft"), {{QStringLiteral("target"), QStringLiteral("profile:shooter")}}).code ==
          u"GS-MCP-008");
    REQUIRE(setup.confirmer.shown.size() == 1);
    const QVariantMap details = setup.confirmer.shown.front().details;
    CHECK(details.value(QStringLiteral("conflicts")).toInt() == 1);
    const QVariantList shown = details.value(QStringLiteral("files")).toList();
    REQUIRE(shown.size() == 2);
    CHECK(shown.at(0).toMap().value(QStringLiteral("path")).toString() == u"logic.ts");
    CHECK(shown.at(0).toMap().value(QStringLiteral("conflict")).toBool());

    const McpOutcome discarded = setup.run(QStringLiteral("discard_draft"), {{QStringLiteral("target"), QStringLiteral("profile:shooter")}});
    CHECK_FALSE(discarded.failed);
    CHECK_FALSE(setup.drafts.has(kClient, QStringLiteral("profile:shooter")));
    CHECK_FALSE(fs::exists(setup.drafts.folderOf(kClient, QStringLiteral("profile:shooter"))));
    CHECK(readFile(made / "logic.ts") == "log.info(\"changed by the user\");\n");
    CHECK_FALSE(fs::exists(made / "notes.md"));
    CHECK(setup.run(QStringLiteral("discard_draft"), {{QStringLiteral("target"), QStringLiteral("profile:shooter")}}).code ==
          u"GS-MCP-014");
}

TEST_CASE("an applied draft keeps a file's line endings and byte order mark, and turns a single-file profile into a folder")
{
    Setup setup;
    const fs::path made = setup.profileFolder("crlf");
    writeFile(made / "notes.md", "\xEF\xBB\xBFone\r\ntwo\r\n");
    setup.library.reload();
    setup.confirmer.answer = McpAnswer::Allowed;
    REQUIRE_FALSE(setup.run(QStringLiteral("write_profile"),
                            {{QStringLiteral("profile"), QStringLiteral("crlf")},
                             {QStringLiteral("files"), QJsonObject{{QStringLiteral("notes.md"), QStringLiteral("one\ntwo\nthree\n")}}}})
                      .failed);
    CHECK(readFile(made / "notes.md") == "\xEF\xBB\xBFone\r\ntwo\r\n");
    CHECK_FALSE(setup.run(QStringLiteral("apply_draft"), {{QStringLiteral("target"), QStringLiteral("profile:crlf")}}).failed);
    CHECK(readFile(made / "notes.md") == "\xEF\xBB\xBFone\r\ntwo\r\nthree\r\n");

    writeFile(setup.folder.path() / "profiles" / "plain.json", kProfile);
    setup.library.reload();
    REQUIRE(setup.library.find(QStringLiteral("plain")) != nullptr);
    REQUIRE_FALSE(setup.run(QStringLiteral("write_profile"),
                            {{QStringLiteral("profile"), QStringLiteral("plain")},
                             {QStringLiteral("files"), QJsonObject{{QStringLiteral("logic.ts"), QStringLiteral("log.info(\"x\");\n")}}}})
                      .failed);
    const McpOutcome grown = setup.run(QStringLiteral("apply_draft"), {{QStringLiteral("target"), QStringLiteral("plain")}});
    CAPTURE(textOf(grown).toStdString());
    CHECK_FALSE(grown.failed);
    CHECK(setup.confirmer.shown.back().details.value(QStringLiteral("folder")).toBool());
    CHECK(fs::is_regular_file(setup.folder.path() / "profiles" / "plain" / "profile.json"));
    CHECK(readFile(setup.folder.path() / "profiles" / "plain" / "logic.ts") == "log.info(\"x\");\n");
    CHECK_FALSE(fs::exists(setup.folder.path() / "profiles" / "plain.json"));
}

TEST_CASE("write_profile refuses a read-only profile from an extension")
{
    Setup setup;
    setup.library.setExtensionProfiles({gs::app::ExtensionProfile{.id = QStringLiteral("gstest.pack/Racer"),
                                                                  .extensionId = QStringLiteral("gstest.pack"),
                                                                  .extensionName = QStringLiteral("Test pack"),
                                                                  .version = QStringLiteral("1.0.0"),
                                                                  .json = QByteArray(kProfile)}});
    REQUIRE(setup.library.find(QStringLiteral("gstest.pack/Racer")) != nullptr);
    setup.confirmer.answer = McpAnswer::Allowed;
    const McpOutcome refused = setup.run(QStringLiteral("write_profile"),
                                         {{QStringLiteral("profile"), QStringLiteral("gstest.pack/Racer")},
                                          {QStringLiteral("files"), QJsonObject{{QStringLiteral("profile.json"),
                                                                                 QString::fromUtf8(kProfile)}}}});
    CHECK(refused.code == u"GS-MCP-007");
    CHECK(setup.confirmer.shown.empty());
    CHECK(setup.drafts.targets(kClient).isEmpty());
    const McpOutcome read = setup.run(QStringLiteral("read_profile"), {{QStringLiteral("profile"), QStringLiteral("gstest.pack/Racer")}});
    CHECK(QJsonDocument::fromJson(textOf(read).toUtf8()).object().value(QStringLiteral("readOnly")).toBool());
}

TEST_CASE("write_component drafts a new component, and apply_draft adds it to My components")
{
    Setup setup;
    const QString code = QStringLiteral("export default class Wave extends Component {\n    setup() {\n"
                                        "        log.info(\"wave\");\n    }\n}\n");
    REQUIRE_FALSE(setup.run(QStringLiteral("write_component"),
                            {{QStringLiteral("component"), QStringLiteral("mine/wave")},
                             {QStringLiteral("files"), QJsonObject{{QStringLiteral("component.ts"), code}}}})
                      .failed);
    CHECK(setup.components.find(QStringLiteral("mine/wave")) == nullptr);
    const QJsonArray listed = QJsonDocument::fromJson(textOf(setup.run(QStringLiteral("list_components"))).toUtf8()).array();
    REQUIRE(listed.size() == 1);
    CHECK(listed.at(0).toObject().value(QStringLiteral("ref")).toString() == u"mine/wave");
    CHECK(listed.at(0).toObject().value(QStringLiteral("new")).toBool());

    REQUIRE_FALSE(setup.run(QStringLiteral("write_component"),
                            {{QStringLiteral("component"), QStringLiteral("mine/empty")},
                             {QStringLiteral("files"), QJsonObject{{QStringLiteral("notes.md"), QStringLiteral("later\n")}}}})
                      .failed);
    setup.confirmer.answer = McpAnswer::Allowed;
    CHECK(setup.run(QStringLiteral("apply_draft"), {{QStringLiteral("target"), QStringLiteral("component:mine/empty")}}).code ==
          u"GS-MCP-004");
    CHECK(setup.run(QStringLiteral("write_component"),
                    {{QStringLiteral("component"), QStringLiteral("Bad Name")},
                     {QStringLiteral("files"), QJsonObject{{QStringLiteral("component.ts"), code}}}})
              .code == u"GS-MCP-019");

    const McpOutcome applied = setup.run(QStringLiteral("apply_draft"), {{QStringLiteral("target"), QStringLiteral("component:mine/wave")}});
    CAPTURE(textOf(applied).toStdString());
    REQUIRE_FALSE(applied.failed);
    REQUIRE(setup.components.find(QStringLiteral("mine/wave")) != nullptr);
    CHECK(readFile(setup.components.folderOf(QStringLiteral("mine/wave")) / "component.ts") == code.toStdString());
    const QJsonObject read = objectOf(setup.run(QStringLiteral("read_component"), {{QStringLiteral("component"), QStringLiteral("mine/wave")}}));
    CHECK(read.value(QStringLiteral("files")).toObject().value(QStringLiteral("component.ts")).toString() == code);
}

TEST_CASE("edit_blueprint adds a block and connects it in the draft, which changes the code and the layout file")
{
    Setup setup;
    const fs::path made = setup.folder.path() / "profiles" / "blue";
    const std::string original = "keys.F.onPress(async () => {\n    await touch.tap(0.5, 0.5);\n});\n";
    writeFile(made / "profile.json", kProfile);
    writeFile(made / "logic.ts", original);
    setup.library.reload();

    const McpOutcome read = setup.run(QStringLiteral("read_blueprint"), {{QStringLiteral("target"), QStringLiteral("profile:blue")},
                                                                         {QStringLiteral("catalog"), QStringLiteral("sleep")}});
    CAPTURE(textOf(read).toStdString());
    REQUIRE_FALSE(read.failed);
    const QJsonObject blueprint = objectOf(read);
    QString tap;
    for (const QJsonValue& node : blueprint.value(QStringLiteral("graphs")).toArray().at(0).toObject().value(QStringLiteral("nodes")).toArray()) {
        if (node.toObject().value(QStringLiteral("key")).toString().contains(QStringLiteral("touch.tap"))) {
            tap = node.toObject().value(QStringLiteral("key")).toString();
        }
    }
    QString sleep;
    for (const QJsonValue& entry : blueprint.value(QStringLiteral("catalog")).toArray()) {
        const QJsonObject item = entry.toObject();
        if (sleep.isEmpty() && (item.value(QStringLiteral("id")).toString() == u"call:sleep" ||
                                item.value(QStringLiteral("call")).toString() == u"sleep")) {
            sleep = item.value(QStringLiteral("id")).toString();
        }
    }
    REQUIRE_FALSE(tap.isEmpty());
    REQUIRE_FALSE(sleep.isEmpty());

    const QJsonArray operations{
        QJsonObject{{QStringLiteral("op"), QStringLiteral("add")}, {QStringLiteral("id"), sleep},
                    {QStringLiteral("x"), 400}, {QStringLiteral("y"), 200}},
        QJsonObject{{QStringLiteral("op"), QStringLiteral("connect")}, {QStringLiteral("from"), tap},
                    {QStringLiteral("fromPin"), QStringLiteral("exec")}, {QStringLiteral("to"), QStringLiteral("$0")},
                    {QStringLiteral("toPin"), QStringLiteral("exec")}},
        QJsonObject{{QStringLiteral("op"), QStringLiteral("comment")}, {QStringLiteral("nodes"), QJsonArray{tap}},
                    {QStringLiteral("title"), QStringLiteral("Tap then wait")}}};
    const McpOutcome edited = setup.run(QStringLiteral("edit_blueprint"), {{QStringLiteral("target"), QStringLiteral("profile:blue")},
                                                                           {QStringLiteral("operations"), operations}});
    CAPTURE(textOf(edited).toStdString());
    REQUIRE_FALSE(edited.failed);
    CHECK(objectOf(edited).value(QStringLiteral("codeChanged")).toBool());
    const fs::path draft = setup.drafts.folderOf(kClient, QStringLiteral("profile:blue"));
    const std::string code = readFile(draft / "logic.ts");
    CAPTURE(code);
    const auto tapAt = code.find("touch.tap(0.5, 0.5)");
    const auto sleepAt = code.find("sleep(");
    REQUIRE(tapAt != std::string::npos);
    REQUIRE(sleepAt != std::string::npos);
    CHECK(tapAt < sleepAt);
    CHECK(readFile(made / "logic.ts") == original);
    const std::string layout = readFile(draft / "logic.blueprint.json");
    CHECK(layout.find("gamescrcpy.blueprint") != std::string::npos);
    CHECK(layout.find("Tap then wait") != std::string::npos);
    CHECK_FALSE(fs::exists(made / "logic.blueprint.json"));
    const std::vector<McpDraftFile> files = setup.drafts.diff(kClient, QStringLiteral("profile:blue"));
    REQUIRE(fileIn(files, QStringLiteral("logic.ts")) != nullptr);
    REQUIRE(fileIn(files, QStringLiteral("logic.blueprint.json")) != nullptr);
    CHECK(fileIn(files, QStringLiteral("logic.blueprint.json"))->state == u"added");

    const McpOutcome wrong = setup.run(QStringLiteral("edit_blueprint"),
                                       {{QStringLiteral("target"), QStringLiteral("profile:blue")},
                                        {QStringLiteral("operations"), QJsonArray{QJsonObject{{QStringLiteral("op"), QStringLiteral("fly")}}}}});
    CHECK(wrong.code == u"GS-MCP-016");
    CHECK(readFile(draft / "logic.ts") == code);
    const McpOutcome drafted = setup.run(QStringLiteral("read_blueprint"), {{QStringLiteral("target"), QStringLiteral("profile:blue")},
                                                                            {QStringLiteral("draft"), true}});
    REQUIRE_FALSE(drafted.failed);
    CHECK(objectOf(drafted).value(QStringLiteral("draft")).toBool());
}

TEST_CASE("read_blueprint reads a component's own file as its page and edit_blueprint adds an event there (ADR-0091)")
{
    Setup setup;
    const fs::path made = setup.folder.path() / "profiles" / "blue";
    writeFile(made / "profile.json", kProfile);
    writeFile(made / "logic.ts", "keys.F.onPress(async () => {\n    await touch.tap(0.5, 0.5);\n});\n");
    setup.library.reload();

    const McpOutcome read = setup.run(QStringLiteral("read_blueprint"), {{QStringLiteral("target"), QStringLiteral("profile:blue")},
                                                                       {QStringLiteral("file"), QStringLiteral("controls/jump.ts")},
                                                                       {QStringLiteral("catalog"), QStringLiteral("onPress")}});
    CAPTURE(textOf(read).toStdString());
    REQUIRE_FALSE(read.failed);
    const QJsonObject blueprint = objectOf(read);
    const QJsonArray pages = blueprint.value(QStringLiteral("pages")).toArray();
    REQUIRE(pages.size() == 1);
    CHECK(pages.first().toObject().value(QStringLiteral("title")).toString() == QStringLiteral("Jump · Space"));
    bool event = false;
    for (const QJsonValue& entry : blueprint.value(QStringLiteral("catalog")).toArray()) {
        if (entry.toObject().value(QStringLiteral("id")).toString() == u"event:controls.jump.onPress") {
            event = true;
        }
    }
    CHECK(event);

    const QJsonArray operations{
        QJsonObject{{QStringLiteral("op"), QStringLiteral("add")},
                    {QStringLiteral("id"), QStringLiteral("event:controls.jump.onPress")},
                    {QStringLiteral("page"), QString()},
                    {QStringLiteral("x"), 300}, {QStringLiteral("y"), 80}}};
    const McpOutcome edited = setup.run(QStringLiteral("edit_blueprint"),
                                        {{QStringLiteral("target"), QStringLiteral("profile:blue")},
                                         {QStringLiteral("file"), QStringLiteral("controls/jump.ts")},
                                         {QStringLiteral("operations"), operations}});
    CAPTURE(textOf(edited).toStdString());
    REQUIRE_FALSE(edited.failed);
    const fs::path draft = setup.drafts.folderOf(kClient, QStringLiteral("profile:blue"));
    const std::string code = readFile(draft / "controls" / "jump.ts");
    CAPTURE(code);
    CHECK(code.find("controls.jump.onPress(") != std::string::npos);
    CHECK(readFile(draft / "logic.ts").find("controls.jump") == std::string::npos);
}

TEST_CASE("check returns the problems gs check finds in a broken file")
{
    Setup setup;
    const fs::path made = setup.folder.path() / "profiles" / "broken";
    writeFile(made / "profile.json", kProfile);
    writeFile(made / "logic.ts", "enum Mode { Fast }\nlog.info(Mode.Fast);\n");
    writeFile(made / "tsconfig.json", gs::script::profileTsconfig());
    setup.library.reload();
    const McpOutcome checked = setup.run(QStringLiteral("check"), {{QStringLiteral("target"), QStringLiteral("profile:broken")}});
    CAPTURE(textOf(checked).toStdString());
    REQUIRE_FALSE(checked.failed);
    const QJsonObject result = objectOf(checked);
    CHECK_FALSE(result.value(QStringLiteral("ok")).toBool());
    bool found = false;
    for (const QJsonValue& value : result.value(QStringLiteral("problems")).toArray()) {
        const QJsonObject problem = value.toObject();
        found = found || (problem.value(QStringLiteral("code")).toString() == u"GS0001" &&
                          problem.value(QStringLiteral("file")).toString() == u"logic.ts" &&
                          problem.value(QStringLiteral("line")).toInt() == 1);
    }
    CHECK(found);
    CHECK(setup.run(QStringLiteral("check"), {{QStringLiteral("target"), QStringLiteral("profile:broken")},
                                              {QStringLiteral("draft"), true}})
              .code == u"GS-MCP-014");
    CHECK(setup.run(QStringLiteral("check"), {{QStringLiteral("target"), QStringLiteral("profile:nope")}}).code == u"GS-MCP-006");
}

TEST_CASE("find_image finds a profile template and a given PNG in a recorded frame, with the result shape of vision.find")
{
    Setup setup;
    const std::shared_ptr<gs::media::LumaSnapshot> frame = loadFrame("frame_h265_top.pgm");
    setup.devices.frame = frame;
    const fs::path profile = setup.profileFolder("vision");
    const QByteArray bell = pngOf(cropOf(*frame, 21, 378, 20, 22));
    writeFile(profile / "res" / "bell.png", bell.toStdString());
    REQUIRE_FALSE(static_cast<bool>(gs::script::saveResources(
        profile / "res" / "resources.json",
        gs::script::Resources{.templates = {gs::script::TemplateResource{.name = "bell", .file = "bell.png"}}})));
    setup.phone(profile);

    const auto check = [](const McpOutcome& outcome) {
        CAPTURE(textOf(outcome).toStdString());
        REQUIRE_FALSE(outcome.failed);
        const QJsonObject found = QJsonDocument::fromJson(textOf(outcome).toUtf8()).object();
        CHECK(found.value(QStringLiteral("x")).toDouble() == doctest::Approx(31.0 / 288.0).epsilon(0.001));
        CHECK(found.value(QStringLiteral("y")).toDouble() == doctest::Approx(389.0 / 640.0).epsilon(0.001));
        CHECK(found.value(QStringLiteral("width")).toDouble() == doctest::Approx(20.0 / 288.0).epsilon(0.001));
        CHECK(found.value(QStringLiteral("height")).toDouble() == doctest::Approx(22.0 / 640.0).epsilon(0.001));
        CHECK(found.value(QStringLiteral("score")).toDouble() > 0.95);
    };
    check(setup.run(QStringLiteral("find_image"), {{QStringLiteral("image"), QStringLiteral("bell")}}));
    check(setup.run(QStringLiteral("find_image"), {{QStringLiteral("image"), QStringLiteral("res/bell.png")}}));
    check(setup.run(QStringLiteral("find_image"), {{QStringLiteral("png"), QString::fromLatin1(bell.toBase64())}}));

    const McpOutcome all = setup.run(QStringLiteral("find_image"),
                                     {{QStringLiteral("image"), QStringLiteral("bell")}, {QStringLiteral("all"), true}});
    REQUIRE_FALSE(all.failed);
    CHECK(QJsonDocument::fromJson(textOf(all).toUtf8()).array().size() == 1);

    const McpOutcome elsewhere = setup.run(
        QStringLiteral("find_image"),
        {{QStringLiteral("image"), QStringLiteral("bell")},
         {QStringLiteral("region"), QJsonObject{{QStringLiteral("x"), 0.5}, {QStringLiteral("y"), 0.0},
                                                {QStringLiteral("width"), 0.5}, {QStringLiteral("height"), 0.5}}}});
    CHECK(textOf(elsewhere) == u"null");

    CHECK(setup.run(QStringLiteral("find_image"), {{QStringLiteral("image"), QStringLiteral("nothing")}}).code == u"GS-API-018");
    CHECK(setup.run(QStringLiteral("find_image"), {{QStringLiteral("image"), QStringLiteral("bell")},
                                                   {QStringLiteral("match"), QStringLiteral("rgb")}})
              .code == u"GS-API-019");
    setup.devices.phones.clear();
    CHECK(setup.run(QStringLiteral("find_image"), {{QStringLiteral("image"), QStringLiteral("bell")}}).code == u"GS-MCP-005");
}

TEST_CASE("get_frame returns the phone's lossless screenshot, or the latest video frame when there is none")
{
    Setup setup;
    setup.phone();
    setup.devices.frame = colored(loadFrame("frame_h265_top.pgm"));

    QImage red(4, 8, QImage::Format_RGB32);
    red.fill(Qt::red);
    setup.devices.canShoot = true;
    setup.devices.shot = red;
    const McpOutcome lossless = setup.run(QStringLiteral("get_frame"));
    REQUIRE_FALSE(lossless.failed);
    REQUIRE(lossless.content.size() == 2);
    CHECK(textOf(lossless).startsWith(QStringLiteral("4 x 8 pixels: the phone's lossless screenshot")));
    const QJsonObject image = lossless.content.at(1).toObject();
    CHECK(image.value(QStringLiteral("type")).toString() == u"image");
    CHECK(image.value(QStringLiteral("mimeType")).toString() == u"image/png");
    const QImage decoded = QImage::fromData(QByteArray::fromBase64(image.value(QStringLiteral("data")).toString().toLatin1()));
    CHECK(decoded.size() == QSize(4, 8));
    CHECK(decoded.pixelColor(1, 1) == QColor(Qt::red));

    setup.devices.canShoot = false;
    const McpOutcome video = setup.run(QStringLiteral("get_frame"), {{QStringLiteral("maxSide"), 64}});
    REQUIRE_FALSE(video.failed);
    CHECK(textOf(video).contains(QStringLiteral(" x 64 pixels, scaled down from 288 x 640: the latest frame")));

    setup.devices.canShoot = true;
    setup.devices.shot.reset();
    const std::shared_ptr<Reply> waiting = setup.start(QStringLiteral("get_frame"));
    CHECK(gs::test::waitUntil([&setup] { return static_cast<bool>(setup.devices.waitingShot); }));
    CHECK_FALSE(waiting->done);
    setup.clock.advance(McpTools::kScreenshotWait);
    setup.tools.expire();
    REQUIRE(gs::test::waitUntil([&waiting] { return waiting->done; }));
    CHECK(textOf(waiting->outcome).contains(QStringLiteral("288 x 640 pixels: the latest frame")));
    setup.devices.waitingShot(red);

    setup.devices.frame = nullptr;
    setup.devices.canShoot = false;
    CHECK(setup.run(QStringLiteral("get_frame")).code == u"GS-MCP-012");
}

TEST_CASE("run_script asks every time, runs on the phone, returns the console and stops at its time limit")
{
    Setup setup;
    const fs::path profile = setup.profileFolder("logic");
    setup.phone(profile);

    setup.confirmer.answer = McpAnswer::Refused;
    const McpOutcome refused = setup.run(QStringLiteral("run_script"), {{QStringLiteral("code"), QStringLiteral("log.info(1);")}});
    CHECK(refused.code == u"GS-MCP-008");
    CHECK(setup.devices.jobs.empty());
    REQUIRE(setup.confirmer.shown.size() == 1);
    CHECK(setup.confirmer.shown.front().kind == u"run");
    CHECK(setup.confirmer.shown.front().details.value(QStringLiteral("phone")).toString() == u"Demo phone");
    CHECK(setup.confirmer.shown.front().details.value(QStringLiteral("code")).toString() == u"log.info(1);");
    CHECK(setup.confirmer.shown.front().details.value(QStringLiteral("quiet")).toBool());

    setup.confirmer.answer = McpAnswer::Allowed;
    const std::shared_ptr<Reply> running =
        setup.start(QStringLiteral("run_script"), {{QStringLiteral("code"), QStringLiteral("log.info(\"hello\");")}});
    REQUIRE(setup.devices.jobs.size() == 1);
    CHECK(setup.devices.startedOn == u"demo");
    ScriptJob& job = setup.devices.jobs.back();
    CHECK(job.entry == "main.ts");
    CHECK(job.handler == "default");
    const std::string module = readFile(job.folder / "main.ts");
    CHECK(module.starts_with("log.info(\"hello\");"));
    CHECK(module.find("export default async function") != std::string::npos);
    job.reported(gs::app::LogicReport{.level = gs::log::Level::Info, .text = QStringLiteral("hello"),
                                      .file = QStringLiteral("main.ts"), .line = 1});
    job.ended(QStringLiteral("done"), {});
    REQUIRE(running->done);
    CHECK_FALSE(running->outcome.failed);
    CHECK(textOf(running->outcome) == u"info main.ts:1: hello\nThe script finished.");
    CHECK_FALSE(fs::exists(job.folder));

    const std::shared_ptr<Reply> slow = setup.start(QStringLiteral("run_script"),
                                                    {{QStringLiteral("function"), QStringLiteral("start")},
                                                     {QStringLiteral("timeoutMs"), 1000}});
    REQUIRE(setup.devices.jobs.size() == 2);
    CHECK(setup.devices.jobs.back().folder == profile);
    CHECK(setup.devices.jobs.back().entry == "logic.ts");
    CHECK(setup.devices.jobs.back().handler == "start");
    CHECK(setup.confirmer.shown.back().details.value(QStringLiteral("code")).toString().contains(QStringLiteral("controls.jump")));
    setup.clock.advance(std::chrono::milliseconds(999));
    setup.tools.expire();
    CHECK_FALSE(slow->done);
    setup.clock.advance(std::chrono::milliseconds(1));
    setup.tools.expire();
    REQUIRE(slow->done);
    CHECK(setup.devices.stopped == std::vector<quint64>{2});
    CHECK(slow->outcome.code == u"GS-MCP-013");
    CHECK(textOf(slow->outcome).contains(QStringLiteral("time limit of 1000 ms")));

    CHECK(setup.run(QStringLiteral("run_script"), {{QStringLiteral("function"), QStringLiteral("not a name")}}).code ==
          u"GS-MCP-004");
    CHECK(setup.run(QStringLiteral("run_script"), {{QStringLiteral("code"), QStringLiteral("x")},
                                                   {QStringLiteral("function"), QStringLiteral("start")}})
              .code == u"GS-MCP-004");
    setup.devices.refusal = QStringLiteral("Demo phone is not mirroring. Mirror it to run the script");
    CHECK(setup.run(QStringLiteral("run_script"), {{QStringLiteral("code"), QStringLiteral("x")}}).code == u"GS-MCP-005");
}

TEST_CASE("run_script stops asking until GameScrcpy closes when the user ticks the box, and asks again once it is "
          "taken back")
{
    Setup setup;
    setup.phone(setup.profileFolder("logic"));
    setup.confirmer.answer = McpAnswer::AllowedForAWhile;
    const std::shared_ptr<Reply> first = setup.start(QStringLiteral("run_script"), {{QStringLiteral("code"), QStringLiteral("log.info(1);")}});
    REQUIRE(setup.devices.jobs.size() == 1);
    CHECK(setup.confirmer.shown.size() == 1);
    setup.devices.jobs.back().ended(QStringLiteral("done"), {});
    REQUIRE(first->done);
    const QVariantList allowed = setup.tools.allowedNow();
    REQUIRE(allowed.size() == 1);
    const QVariantMap grant = allowed.front().toMap();
    CHECK(grant.value(QStringLiteral("client")).toString() == kClient);
    CHECK(grant.value(QStringLiteral("kind")).toString() == u"run");

    setup.confirmer.answer = McpAnswer::Refused;
    setup.clock.advance(std::chrono::hours(24));
    const std::shared_ptr<Reply> second = setup.start(QStringLiteral("run_script"), {{QStringLiteral("code"), QStringLiteral("log.info(2);")}});
    REQUIRE(setup.devices.jobs.size() == 2);
    CHECK(setup.confirmer.shown.size() == 1);
    setup.devices.jobs.back().ended(QStringLiteral("done"), {});
    REQUIRE(second->done);
    CHECK_FALSE(second->outcome.failed);

    setup.tools.endAllowed(kClient, grant.value(QStringLiteral("key")).toString());
    CHECK(setup.tools.allowedNow().isEmpty());
    CHECK(setup.run(QStringLiteral("run_script"), {{QStringLiteral("code"), QStringLiteral("log.info(3);")}}).code == u"GS-MCP-008");
    CHECK(setup.confirmer.shown.size() == 2);
    CHECK(setup.devices.jobs.size() == 2);

    setup.confirmer.answer = McpAnswer::AllowedForAWhile;
    const std::shared_ptr<Reply> third = setup.start(QStringLiteral("run_script"), {{QStringLiteral("code"), QStringLiteral("log.info(4);")}});
    REQUIRE(setup.devices.jobs.size() == 3);
    setup.devices.jobs.back().ended(QStringLiteral("done"), {});
    REQUIRE(third->done);
    setup.tools.forget(kClient);
    CHECK(setup.tools.allowedNow().isEmpty());
    setup.confirmer.answer = McpAnswer::Refused;
    CHECK(setup.run(QStringLiteral("run_script"), {{QStringLiteral("code"), QStringLiteral("log.info(5);")}}).code == u"GS-MCP-008");
    CHECK(setup.confirmer.shown.size() == 4);
}

TEST_CASE("cancelling a run_script stops the script without an answer")
{
    Setup setup;
    setup.phone(setup.profileFolder("logic"));
    setup.confirmer.answer = McpAnswer::Allowed;
    const std::shared_ptr<Reply> running =
        setup.start(QStringLiteral("run_script"), {{QStringLiteral("code"), QStringLiteral("await sleep(10000);")}});
    REQUIRE(setup.devices.jobs.size() == 1);
    setup.tools.cancel(setup.lastCall);
    CHECK(setup.devices.stopped == std::vector<quint64>{1});
    CHECK_FALSE(running->done);
}

TEST_CASE("start_recording asks the user first, and stop_recording returns the recording for run_replay")
{
    Setup setup;
    setup.phone();
    setup.confirmer.answer = McpAnswer::Refused;
    CHECK(setup.run(QStringLiteral("start_recording")).code == u"GS-MCP-008");
    CHECK_FALSE(setup.devices.recordingNow);
    REQUIRE(setup.confirmer.shown.size() == 1);
    CHECK(setup.confirmer.shown.front().kind == u"record");
    CHECK(setup.confirmer.shown.front().details.value(QStringLiteral("phone")).toString() == u"Demo phone");
    CHECK(setup.run(QStringLiteral("stop_recording")).code == u"GS-MCP-017");

    setup.confirmer.answer = McpAnswer::Allowed;
    CHECK_FALSE(setup.run(QStringLiteral("start_recording")).failed);
    CHECK(setup.devices.recordingNow);
    CHECK(setup.devices.recordedOn == u"demo");
    CHECK(textOf(setup.run(QStringLiteral("start_recording"))).contains(QStringLiteral("already")));
    CHECK(setup.confirmer.shown.size() == 2);

    const McpOutcome stopped = setup.run(QStringLiteral("stop_recording"));
    REQUIRE_FALSE(stopped.failed);
    const QJsonObject result = objectOf(stopped);
    CHECK(result.value(QStringLiteral("id")).toString() == u"Demo phone 2026-11-06 10.00.00.gsrec");
    CHECK(result.value(QStringLiteral("folder")).toString() == u"C:/recordings/Demo phone 2026-11-06 10.00.00.gsrec");
    CHECK(result.value(QStringLiteral("session")).toString() == result.value(QStringLiteral("folder")).toString());
    CHECK(result.value(QStringLiteral("video")).toBool());
    CHECK_FALSE(setup.devices.recordingNow);

    const fs::path recordings = setup.folder.path() / "recordings";
    writeFile(recordings / "Demo phone 2026-11-06 10.00.00.gsrec" / "session.json",
              readFile(pathOf(scriptData(QStringLiteral("profile-demo/sessions/basic.json")))));
    fs::create_directories(recordings / "not a recording");
    setup.devices.recordings = QString::fromStdWString(recordings.wstring());
    const QJsonArray listed = objectOf(setup.run(QStringLiteral("list_recordings"))).value(QStringLiteral("recordings")).toArray();
    REQUIRE(listed.size() == 1);
    CHECK(listed.at(0).toObject().value(QStringLiteral("id")).toString() == u"Demo phone 2026-11-06 10.00.00.gsrec");
    CHECK_FALSE(listed.at(0).toObject().value(QStringLiteral("video")).toBool());
}

TEST_CASE("propose_keymap shows dashed controls in the editor; accepted ones join the profile, undo takes them out")
{
    Setup setup;
    setup.phone(setup.profileFolder("logic"));
    const gs::input::LoadResult loaded = gs::input::loadProfile(kProfile);
    REQUIRE(loaded.ok());
    ProfileEditor editor(loaded.profile, gs::input::ScreenGeometry{.width = 2400, .height = 1080});
    setup.devices.editor = &editor;

    const McpOutcome bad = setup.run(QStringLiteral("propose_keymap"),
                                     {{QStringLiteral("controls"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("nonsense")}}}}});
    CHECK(bad.code == u"GS-CHECK-005");
    CHECK(editor.suggestionMarkers(2400, 1080).isEmpty());

    const QJsonArray controls{
        QJsonObject{{QStringLiteral("type"), QStringLiteral("tap")}, {QStringLiteral("name"), QStringLiteral("Pick up")},
                    {QStringLiteral("x"), 0.8}, {QStringLiteral("y"), 0.6},
                    {QStringLiteral("inputs"), QJsonObject{{QStringLiteral("press"), QStringLiteral("KeyE")}}}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("hold")}, {QStringLiteral("name"), QStringLiteral("Crouch")},
                    {QStringLiteral("anchor"), QStringLiteral("bottomLeft")}, {QStringLiteral("offset"), QJsonArray{0.1, -0.1}},
                    {QStringLiteral("inputs"), QJsonObject{{QStringLiteral("press"), QStringLiteral("KeyC")}}}}};
    const McpOutcome shown = setup.run(QStringLiteral("propose_keymap"), {{QStringLiteral("controls"), controls}});
    CAPTURE(textOf(shown).toStdString());
    REQUIRE_FALSE(shown.failed);
    CHECK(setup.devices.editorOn == u"demo");
    const QString id = objectOf(shown).value(QStringLiteral("id")).toString();
    REQUIRE_FALSE(id.isEmpty());

    const QVariantList markers = editor.suggestionMarkers(2400, 1080);
    REQUIRE(markers.size() == 2);
    const QVariantMap first = markers.at(0).toMap();
    CHECK(first.value(QStringLiteral("name")).toString() == u"Pick up");
    CHECK(first.value(QStringLiteral("x")).toDouble() == doctest::Approx(1920.0).epsilon(0.01));
    CHECK(first.value(QStringLiteral("y")).toDouble() == doctest::Approx(648.0).epsilon(0.01));
    CHECK(first.value(QStringLiteral("keyText")).toString().contains(u'E'));

    const std::size_t before = editor.profile().controls.size();
    REQUIRE(editor.placeSuggestion(first.value(QStringLiteral("id")).toString(), 0.7, 0.5));
    REQUIRE(editor.acceptSuggestion(first.value(QStringLiteral("id")).toString()));
    CHECK_FALSE(editor.acceptSuggestion(first.value(QStringLiteral("id")).toString()));
    REQUIRE(editor.profile().controls.size() == before + 1);
    CHECK(editor.profile().controls.back().name == "Pick up");
    CHECK(editor.dirty());
    REQUIRE(editor.rejectSuggestion(markers.at(1).toMap().value(QStringLiteral("id")).toString()));
    CHECK(editor.profile().controls.size() == before + 1);
    CHECK(editor.suggestionMarkers(2400, 1080).isEmpty());

    QJsonArray status = objectOf(setup.run(QStringLiteral("keymap_draft_status"), {{QStringLiteral("id"), id}}))
                            .value(QStringLiteral("controls"))
                            .toArray();
    REQUIRE(status.size() == 2);
    CHECK(status.at(0).toObject().value(QStringLiteral("state")).toString() == u"accepted");
    CHECK(status.at(0).toObject().value(QStringLiteral("inProfile")).toBool());
    CHECK(status.at(1).toObject().value(QStringLiteral("state")).toString() == u"rejected");

    editor.undo();
    CHECK(editor.profile().controls.size() == before);
    status = objectOf(setup.run(QStringLiteral("keymap_draft_status"), {{QStringLiteral("id"), id}}))
                 .value(QStringLiteral("controls"))
                 .toArray();
    CHECK_FALSE(status.at(0).toObject().value(QStringLiteral("inProfile")).toBool());
    CHECK(setup.run(QStringLiteral("keymap_draft_status"), {{QStringLiteral("id"), QStringLiteral("k99")}}).code == u"GS-MCP-018");
}

TEST_CASE("request_materials asks the user for pictures, colors, regions and points and returns what they gave into "
          "the draft")
{
    Setup setup;
    const fs::path made = setup.profileFolder("logic");
    setup.phone(made);

    const QJsonArray items{
        QJsonObject{{QStringLiteral("name"), QStringLiteral("start button")},
                    {QStringLiteral("kind"), QStringLiteral("template")},
                    {QStringLiteral("note"), QStringLiteral("Bottom right of the title screen")}},
        QJsonObject{{QStringLiteral("name"), QStringLiteral("blood red")}, {QStringLiteral("kind"), QStringLiteral("color")}},
        QJsonObject{{QStringLiteral("name"), QStringLiteral("minimap")}, {QStringLiteral("kind"), QStringLiteral("region")}},
        QJsonObject{{QStringLiteral("name"), QStringLiteral("skill 1")}, {QStringLiteral("kind"), QStringLiteral("point")}}};

    const McpOutcome nothing = setup.run(QStringLiteral("request_materials"), {});
    CHECK(nothing.failed);
    CHECK(nothing.code == u"GS-MCP-004");
    CHECK(textOf(nothing).contains(QStringLiteral("items")));
    CHECK(setup.run(QStringLiteral("request_materials"), {{QStringLiteral("items"), QJsonArray{}}}).code == u"GS-MCP-004");
    CHECK(setup.run(QStringLiteral("request_materials"),
                    {{QStringLiteral("items"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("x")},
                                                                     {QStringLiteral("kind"), QStringLiteral("path")}}}}})
              .code == u"GS-MCP-004");
    CHECK(setup.run(QStringLiteral("request_materials"),
                    {{QStringLiteral("items"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("x")},
                                                                     {QStringLiteral("kind"), QStringLiteral("point")},
                                                                     {QStringLiteral("size"), 3}}}}})
              .code == u"GS-MCP-004");
    CHECK(setup.run(QStringLiteral("request_materials"),
                    {{QStringLiteral("items"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("x")},
                                                                     {QStringLiteral("kind"), QStringLiteral("point")}},
                                                          QJsonObject{{QStringLiteral("name"), QStringLiteral(" X ")},
                                                                      {QStringLiteral("kind"), QStringLiteral("color")}}}}})
              .code == u"GS-MCP-004");
    CHECK(setup.devices.materialsAsked.empty());

    setup.devices.phones.clear();
    const McpOutcome noPhone = setup.run(QStringLiteral("request_materials"), {{QStringLiteral("items"), items}});
    CHECK(noPhone.code == u"GS-MCP-005");
    setup.phone(made);
    CHECK(setup.run(QStringLiteral("request_materials"),
                    {{QStringLiteral("items"), items}, {QStringLiteral("profile"), QStringLiteral("nobody")}})
              .code == u"GS-MCP-006");
    CHECK(setup.devices.materialsAsked.empty());

    setup.devices.materialsResults = {
        {.name = QStringLiteral("start button"), .kind = QStringLiteral("template"), .state = QStringLiteral("done"),
         .resource = QStringLiteral("start button"),
         .value = QVariantMap{{QStringLiteral("file"), QStringLiteral("res/start button.png")},
                              {QStringLiteral("width"), 96},
                              {QStringLiteral("height"), 40}}},
        {.name = QStringLiteral("blood red"), .kind = QStringLiteral("color"), .state = QStringLiteral("existing"),
         .resource = QStringLiteral("hp red"), .value = QVariantMap{{QStringLiteral("color"), QStringLiteral("#c81e1e")}}},
        {.name = QStringLiteral("minimap"), .kind = QStringLiteral("region"), .state = QStringLiteral("done"),
         .resource = QStringLiteral("minimap"),
         .value = QVariantMap{{QStringLiteral("x"), 0.02}, {QStringLiteral("y"), 0.03},
                              {QStringLiteral("width"), 0.2}, {QStringLiteral("height"), 0.2}}},
        {.name = QStringLiteral("skill 1"), .kind = QStringLiteral("point"), .state = QStringLiteral("skipped")}};
    const McpOutcome given = setup.run(QStringLiteral("request_materials"), {{QStringLiteral("items"), items}});
    CAPTURE(textOf(given).toStdString());
    REQUIRE_FALSE(given.failed);
    REQUIRE(setup.devices.materialsAsked.size() == 1);
    const gs::app::McpMaterialsRequest& asked = setup.devices.materialsAsked.front();
    CHECK(asked.client == u"Test client");
    CHECK(asked.serial == u"demo");
    CHECK(asked.profileId == u"logic");
    CHECK(asked.profileName == u"Logic demo");
    CHECK(asked.items.size() == 4);
    CHECK(asked.items.at(0).name == u"start button");
    CHECK(asked.items.at(0).kind == u"template");
    CHECK(asked.items.at(0).note == u"Bottom right of the title screen");
    CHECK(asked.items.at(3).kind == u"point");
    CHECK(fs::is_regular_file(asked.folder / "profile.json"));
    CHECK(asked.folder == setup.drafts.folderOf(kClient, QStringLiteral("profile:logic")));
    CHECK_FALSE(fs::exists(made / "res" / "resources.json"));

    const QJsonObject result = objectOf(given);
    CHECK(result.value(QStringLiteral("target")).toString() == u"profile:logic");
    CHECK(result.value(QStringLiteral("name")).toString() == u"Logic demo");
    CHECK(result.value(QStringLiteral("draft")).toBool());
    const QJsonArray said = result.value(QStringLiteral("items")).toArray();
    REQUIRE(said.size() == 4);
    CHECK(said.at(0).toObject().value(QStringLiteral("state")).toString() == u"done");
    CHECK(said.at(0).toObject().value(QStringLiteral("resource")).toString() == u"start button");
    CHECK(said.at(0).toObject().value(QStringLiteral("value")).toObject()
              .value(QStringLiteral("file")).toString() == u"res/start button.png");
    CHECK(said.at(1).toObject().value(QStringLiteral("state")).toString() == u"existing");
    CHECK(said.at(1).toObject().value(QStringLiteral("resource")).toString() == u"hp red");
    CHECK(said.at(3).toObject().value(QStringLiteral("state")).toString() == u"skipped");
    CHECK_FALSE(said.at(3).toObject().contains(QStringLiteral("value")));
    CHECK(result.value(QStringLiteral("note")).toString().contains(QStringLiteral("skipped 1 of 4")));

    setup.devices.materialsWrites = true;
    REQUIRE_FALSE(setup.run(QStringLiteral("request_materials"), {{QStringLiteral("items"), items}}).failed);
    const std::vector<McpDraftFile> changed = setup.drafts.diff(kClient, QStringLiteral("profile:logic"));
    const McpDraftFile* resources = fileIn(changed, QStringLiteral("res/resources.json"));
    REQUIRE(resources != nullptr);
    CHECK(resources->state == u"added");
    CHECK_FALSE(resources->conflict);
    setup.confirmer.answer = McpAnswer::Allowed;
    const McpOutcome applied =
        setup.run(QStringLiteral("apply_draft"), {{QStringLiteral("target"), QStringLiteral("profile:logic")}});
    CAPTURE(textOf(applied).toStdString());
    CHECK_FALSE(applied.failed);
    CHECK(fs::is_regular_file(made / "res" / "resources.json"));

    setup.devices.materialsNow = false;
    setup.devices.materialsRefusal = QStringLiteral("The vision workbench cannot open now");
    CHECK(setup.run(QStringLiteral("request_materials"), {{QStringLiteral("items"), items}}).code == u"GS-MCP-025");
}

TEST_CASE("request_materials waits for the user: one at a time, ended by the caller, by the clock or by a revoke")
{
    Setup setup;
    const fs::path made = setup.profileFolder("logic");
    setup.phone(made);
    const QJsonArray items{QJsonObject{{QStringLiteral("name"), QStringLiteral("fire")},
                                       {QStringLiteral("kind"), QStringLiteral("point")}}};
    setup.devices.materialsNow = false;

    const std::shared_ptr<Reply> first = setup.start(QStringLiteral("request_materials"), {{QStringLiteral("items"), items}});
    REQUIRE(setup.devices.materialsAsked.size() == 1);
    CHECK_FALSE(first->done);
    const quint64 firstId = setup.lastCall;

    const McpOutcome second = setup.run(QStringLiteral("request_materials"), {{QStringLiteral("items"), items}});
    CHECK(second.code == u"GS-MCP-024");
    CHECK(setup.devices.materialsAsked.size() == 1);

    setup.tools.cancel(firstId);
    CHECK(setup.devices.materialsEnded == QStringList{QStringLiteral("Test client")});

    setup.devices.materialsEnded.clear();
    const std::shared_ptr<Reply> waiting = setup.start(QStringLiteral("request_materials"), {{QStringLiteral("items"), items}});
    REQUIRE(setup.devices.materialsAsked.size() == 2);
    setup.clock.advance(std::chrono::minutes(9));
    setup.tools.expire();
    CHECK_FALSE(waiting->done);
    setup.clock.advance(std::chrono::minutes(1));
    setup.tools.expire();
    REQUIRE(waiting->done);
    CHECK(waiting->outcome.code == u"GS-MCP-026");
    CHECK(textOf(waiting->outcome).contains(QStringLiteral("10 minutes")));
    CHECK(setup.devices.materialsEnded == QStringList{QStringLiteral("Test client")});

    setup.devices.materialsEnded.clear();
    const std::shared_ptr<Reply> last = setup.start(QStringLiteral("request_materials"), {{QStringLiteral("items"), items}});
    REQUIRE(setup.devices.materialsAsked.size() == 3);
    setup.tools.forget(kClient);
    CHECK_FALSE(last->done);
    CHECK_FALSE(setup.devices.materialsEnded.isEmpty());
}

TEST_CASE("create_extension makes starters that pass the package check gs validate uses")
{
    Setup setup;
    QString theme;
    for (const QString& kind : {QStringLiteral("theme"), QStringLiteral("filter"), QStringLiteral("component")}) {
        const McpOutcome made = setup.run(QStringLiteral("create_extension"),
                                          {{QStringLiteral("kind"), kind}, {QStringLiteral("name"), QStringLiteral("Night ") + kind}});
        CAPTURE(textOf(made).toStdString());
        REQUIRE_FALSE(made.failed);
        const QJsonObject result = objectOf(made);
        const fs::path folder = pathOf(result.value(QStringLiteral("folder")).toString());
        CHECK(fs::is_regular_file(folder / "manifest.json"));
        const gs::ext::PackageCheck check = gs::ext::checkDirectory(folder);
        for (const gs::ext::Diagnostic& diagnostic : check.diagnostics) {
            CAPTURE(diagnostic.path);
            CAPTURE(diagnostic.detail);
            CHECK(diagnostic.severity != gs::ext::Severity::Error);
        }
        REQUIRE(check.ok());
        CHECK(check.manifest->id == (QStringLiteral("local.night-") + kind).toStdString());
        CHECK(check.manifest->aiAssisted);
        if (kind == u"theme") {
            theme = result.value(QStringLiteral("folder")).toString();
        }
    }
    CHECK(setup.run(QStringLiteral("create_extension"), {{QStringLiteral("kind"), QStringLiteral("theme")},
                                                         {QStringLiteral("name"), QStringLiteral("Night theme")}})
              .code == u"GS-MCP-021");
    CHECK(setup.run(QStringLiteral("create_extension"), {{QStringLiteral("kind"), QStringLiteral("native")},
                                                         {QStringLiteral("name"), QStringLiteral("Dll")}})
              .code == u"GS-MCP-021");

    const McpOutcome written = setup.run(QStringLiteral("write_extension"),
                                         {{QStringLiteral("extension"), theme},
                                          {QStringLiteral("files"), QJsonObject{{QStringLiteral("README.md"), QStringLiteral("# Night\n")}}}});
    CAPTURE(textOf(written).toStdString());
    CHECK_FALSE(written.failed);
    CHECK(readFile(pathOf(theme) / "README.md") == "# Night\n");
    CHECK(setup.run(QStringLiteral("write_extension"),
                    {{QStringLiteral("extension"), QString::fromStdWString(setup.folder.path().wstring())},
                     {QStringLiteral("files"), QJsonObject{{QStringLiteral("README.md"), QStringLiteral("x")}}}})
              .code == u"GS-MCP-004");
}

TEST_CASE("the component starter declares its component as extension packages do now: component 1.0 and a named folder")
{
    Setup setup;
    const McpOutcome made = setup.run(
        QStringLiteral("create_extension"),
        {{QStringLiteral("kind"), QStringLiteral("component")},
         {QStringLiteral("name"), QStringLiteral("Quick Tap: double tap the spot where it sits on the picture, then wait")}});
    CAPTURE(textOf(made).toStdString());
    REQUIRE_FALSE(made.failed);
    const fs::path folder = pathOf(objectOf(made).value(QStringLiteral("folder")).toString());
    const gs::ext::LoadedManifest loaded = gs::ext::loadManifest(readFile(folder / "manifest.json"));
    for (const gs::ext::Diagnostic& diagnostic : loaded.diagnostics) {
        CAPTURE(diagnostic.path);
        CAPTURE(diagnostic.detail);
        CHECK(diagnostic.severity != gs::ext::Severity::Error);
    }
    REQUIRE(loaded.usable);
    const gs::ext::Manifest& manifest = loaded.manifest;
    CHECK(manifest.declares(gs::ext::ExtensionPoint::Component));
    CHECK_FALSE(manifest.declares(gs::ext::ExtensionPoint::Script));
    CHECK(manifest.contributes.scripts.empty());
    CHECK_FALSE(manifest.contributes.native.has_value());
    REQUIRE(manifest.contributes.components.size() == 1);
    const gs::ext::ComponentContribution& component = manifest.contributes.components.front();
    CHECK_FALSE(component.name.empty());
    CHECK(component.name.size() <= gs::ext::kMaxComponentNameLength);
    CHECK(manifest.id == "local." + component.name);
    CHECK(component.folder == "components/" + component.name);
    CHECK(fs::is_regular_file(folder / fs::path(component.folder) / "component.ts"));
    CHECK(fs::is_regular_file(folder / fs::path(component.folder) / "component.json"));
    const gs::ext::PackageCheck check = gs::ext::checkDirectory(folder);
    CHECK(check.ok());
}

TEST_CASE("drafts untouched for 7 days are thrown away when GameScrcpy starts")
{
    Setup setup;
    setup.profileFolder("shooter");
    REQUIRE_FALSE(setup.run(QStringLiteral("write_profile"),
                            {{QStringLiteral("profile"), QStringLiteral("shooter")},
                             {QStringLiteral("files"), QJsonObject{{QStringLiteral("notes.md"), QStringLiteral("x\n")}}}})
                      .failed);
    const fs::path draft = setup.drafts.folderOf(kClient, QStringLiteral("profile:shooter"));
    REQUIRE(fs::is_directory(draft));
    McpDrafts fresh{{.root = setup.folder.path() / "drafts", .profiles = &setup.library}};
    CHECK(fs::is_directory(draft));
    const auto old = fs::file_time_type::clock::now() - std::chrono::hours(24 * 8);
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(draft.parent_path())) {
        if (entry.is_regular_file()) {
            fs::last_write_time(entry.path(), old);
        }
    }
    McpDrafts later{{.root = setup.folder.path() / "drafts", .profiles = &setup.library}};
    CHECK_FALSE(fs::exists(draft));
    CHECK(later.targets(kClient).isEmpty());
}

TEST_CASE("a pending draft names its client and target, and its preview carries the draft and the version in use")
{
    Setup setup;
    const fs::path made = setup.profileFolder("shooter");
    CHECK(setup.drafts.pending().empty());
    const QString changed = QString::fromUtf8(kProfile).replace(QStringLiteral("Logic demo"), QStringLiteral("Draft demo"));
    REQUIRE_FALSE(setup.run(QStringLiteral("write_profile"),
                            {{QStringLiteral("profile"), QStringLiteral("shooter")},
                             {QStringLiteral("files"), QJsonObject{{QStringLiteral("profile.json"), changed}}}})
                      .failed);
    const std::vector<gs::app::McpDraftFacts> pending = setup.drafts.pending();
    REQUIRE(pending.size() == 1);
    CHECK(pending.front().client == kClient);
    CHECK(pending.front().target == u"profile:shooter");

    const std::optional<McpDraftPreview> plan = setup.drafts.preview(kClient, QStringLiteral("profile:shooter"));
    REQUIRE(plan.has_value());
    CHECK(plan->name == u"Logic demo");
    CHECK_FALSE(plan->fresh);
    CHECK(plan->profileText.contains(QStringLiteral("Draft demo")));
    CHECK(plan->liveText.contains(QStringLiteral("Logic demo")));
    CHECK(plan->draftFolder.endsWith(QStringLiteral("profile-shooter")));
    CHECK(plan->liveFolder == QDir::fromNativeSeparators(QString::fromStdWString(made.wstring())));
    CHECK_FALSE(setup.drafts.preview(kClient, QStringLiteral("task:shooter")).has_value());
    CHECK_FALSE(setup.drafts.preview(QStringLiteral("Other client"), QStringLiteral("profile:shooter")).has_value());

    setup.drafts.discard(kClient, QStringLiteral("profile:shooter"));
    CHECK(setup.drafts.pending().empty());
    CHECK_FALSE(setup.drafts.preview(kClient, QStringLiteral("profile:shooter")).has_value());
}

TEST_CASE("open_draft_preview shows the draft in the editor and says whether it opened")
{
    Setup setup;
    setup.profileFolder("shooter");
    REQUIRE_FALSE(setup.run(QStringLiteral("write_profile"),
                            {{QStringLiteral("profile"), QStringLiteral("shooter")},
                             {QStringLiteral("files"), QJsonObject{{QStringLiteral("logic.ts"), QStringLiteral("log.info(1);\n")}}}})
                      .failed);

    setup.devices.previewOpens = false;
    const McpOutcome refused = setup.run(QStringLiteral("open_draft_preview"),
                                         {{QStringLiteral("target"), QStringLiteral("profile:shooter")}});
    CHECK(refused.failed);
    CHECK(refused.code == u"GS-MCP-010");
    CHECK(textOf(refused).contains(QStringLiteral("no mirroring phone")));
    CHECK(setup.devices.previewed == u"profile:shooter");

    setup.devices.previewOpens = true;
    const McpOutcome opened = setup.run(QStringLiteral("open_draft_preview"),
                                        {{QStringLiteral("target"), QStringLiteral("profile:shooter")}});
    CAPTURE(textOf(opened).toStdString());
    CHECK_FALSE(opened.failed);
    CHECK(textOf(opened).contains(QStringLiteral("key mapping editor")));
    CHECK(textOf(opened).contains(QStringLiteral("Nothing changes until the user applies it")));

    CHECK(setup.run(QStringLiteral("open_draft_preview"), {{QStringLiteral("target"), QStringLiteral("profile:nope")}}).code ==
          u"GS-MCP-014");
    CHECK(setup.run(QStringLiteral("open_draft_preview"), {}).code == u"GS-MCP-004");
}

TEST_CASE("unified diffs show the changed lines with three lines of context")
{
    CHECK(gs::app::mcpUnifiedDiff(QStringLiteral("a.ts"), QStringLiteral("one\ntwo\nthree\n"),
                                  QStringLiteral("one\nTWO\nthree\nfour\n")) ==
          QStringLiteral("--- a/a.ts\n+++ b/a.ts\n@@ -1,3 +1,4 @@\n one\n-two\n+TWO\n three\n+four\n"));
    CHECK(gs::app::mcpUnifiedDiff(QStringLiteral("new.ts"), std::nullopt, QStringLiteral("a\nb\n")) ==
          QStringLiteral("--- /dev/null\n+++ b/new.ts\n@@ -0,0 +1,2 @@\n+a\n+b\n"));
    CHECK(gs::app::mcpUnifiedDiff(QStringLiteral("same.ts"), QStringLiteral("x\n"), QStringLiteral("x\r\n")).isEmpty());
    const gs::app::McpLineChange change = gs::app::mcpLineChange(QStringLiteral("a\nb\nc\n"), QStringLiteral("a\nc\nd\ne\n"));
    CHECK(change.added == 2);
    CHECK(change.removed == 1);
}

TEST_CASE("read_logs returns the latest lines oldest first, by level, words and source")
{
    Setup setup;
    const fs::path logs = setup.folder.path() / "logs";
    writeFile(logs / "gamescrcpy.1.log", "2026-11-04 09:59:59.000 info     GameScrcpy 1.0 started\n");
    writeFile(logs / "gamescrcpy.log",
              "2026-11-04 10:00:01.000 warning  logic Demo on Phone: GS-API-003 the duration is too long\n"
              "2026-11-04 10:00:02.000 critical the decoder broke\n"
              "  at frame 12\n"
              "2026-11-04 10:00:03.000 debug    noise\n"
              "2026-11-04 10:00:04.000 info     task Daily on Phone: started\n");
    const QString all = textOf(setup.run(QStringLiteral("read_logs")));
    CHECK(all.startsWith(QStringLiteral("2026-11-04 09:59:59.000 info     GameScrcpy 1.0 started\n")));
    CHECK(all.contains(QStringLiteral("the decoder broke\n  at frame 12")));
    CHECK_FALSE(all.contains(QStringLiteral("noise")));
    CHECK(textOf(setup.run(QStringLiteral("read_logs"), {{QStringLiteral("level"), QStringLiteral("debug")}}))
              .contains(QStringLiteral("noise")));
    CHECK(textOf(setup.run(QStringLiteral("read_logs"), {{QStringLiteral("level"), QStringLiteral("error")}})) ==
          u"2026-11-04 10:00:02.000 critical the decoder broke\n  at frame 12");
    CHECK(textOf(setup.run(QStringLiteral("read_logs"), {{QStringLiteral("contains"), QStringLiteral("gs-api")}}))
              .startsWith(QStringLiteral("2026-11-04 10:00:01.000 warning  logic Demo")));
    const QString logic = textOf(setup.run(QStringLiteral("read_logs"), {{QStringLiteral("source"), QStringLiteral("logic")}}));
    CHECK(logic.split(u'\n').size() == 2);
    CHECK(textOf(setup.run(QStringLiteral("read_logs"), {{QStringLiteral("lines"), 1}})) ==
          u"2026-11-04 10:00:04.000 info     task Daily on Phone: started");
    CHECK(setup.run(QStringLiteral("read_logs"), {{QStringLiteral("level"), QStringLiteral("loud")}}).code == u"GS-MCP-004");
}

TEST_CASE("list_phones and list_profiles describe what is mirroring and what profiles there are")
{
    Setup setup;
    CHECK(QJsonDocument::fromJson(textOf(setup.run(QStringLiteral("list_phones"))).toUtf8()).array().isEmpty());
    setup.phone(setup.profileFolder("logic"));
    const QJsonArray phones = QJsonDocument::fromJson(textOf(setup.run(QStringLiteral("list_phones"))).toUtf8()).array();
    REQUIRE(phones.size() == 1);
    const QJsonObject phone = phones.at(0).toObject();
    CHECK(phone.value(QStringLiteral("serial")).toString() == u"demo");
    CHECK(phone.value(QStringLiteral("width")).toInt() == 1080);
    CHECK(phone.value(QStringLiteral("profile")).toObject().value(QStringLiteral("name")).toString() == u"Logic demo");

    const QJsonArray profiles = QJsonDocument::fromJson(textOf(setup.run(QStringLiteral("list_profiles"))).toUtf8()).array();
    REQUIRE(profiles.size() == 1);
    CHECK(profiles.at(0).toObject().value(QStringLiteral("id")).toString() == u"logic");
    CHECK(profiles.at(0).toObject().value(QStringLiteral("logic")).toBool());
    CHECK_FALSE(profiles.at(0).toObject().value(QStringLiteral("readOnly")).toBool());
    CHECK_FALSE(profiles.at(0).toObject().contains(QStringLiteral("draft")));
}
