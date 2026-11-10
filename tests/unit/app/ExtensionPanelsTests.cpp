#include "ExtensionPanels.h"
#include "LogicServices.h"
#include "ScriptRuntime.h"

#include <doctest/doctest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace gs::app;
using namespace std::chrono_literals;
using gs::ext::PanelPlace;

namespace {

const QString kExtension = QStringLiteral("alice.stats");

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

class IdleLane final : public LaneAccess
{
public:
    void attach(std::shared_ptr<const gs::input::Profile>, std::shared_ptr<LaneObserver>) override {}
    void detach(std::shared_ptr<LaneObserver>) override {}
    void post(std::function<void(InputHost& host, gs::Microseconds now)>) override {}
    [[nodiscard]] const ControlChannel* channel() const noexcept override { return nullptr; }
};

class Folder
{
public:
    Folder() { REQUIRE(m_dir.isValid()); }

    [[nodiscard]] std::filesystem::path path() const { return m_dir.path().toStdWString(); }

    void write(const QString& name, qint64 bytes) const
    {
        REQUIRE(QDir(m_dir.path()).mkpath(QFileInfo(m_dir.filePath(name)).path()));
        QFile file(m_dir.filePath(name));
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(file.write(QByteArray(bytes, 'x')) == bytes);
    }

    void folder(const QString& name) const { REQUIRE(QDir(m_dir.path()).mkpath(name)); }

    void text(const QString& name, const QByteArray& content) const
    {
        REQUIRE(QDir(m_dir.path()).mkpath(QFileInfo(m_dir.filePath(name)).path()));
        QFile file(m_dir.filePath(name));
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(file.write(content) == content.size());
    }

private:
    QTemporaryDir m_dir;
};

struct Logic {
    Logic(const QString& extension, const QString& script, const QString& phone, const std::filesystem::path& folder)
        : runtime(LogicSource{.folder = folder,
                              .entry = "logic.js",
                              .id = script,
                              .name = script,
                              .device = phone,
                              .serial = phone,
                              .extension = extension},
                  std::make_shared<IdleLane>(), std::filesystem::path())
    {
    }

    ScriptRuntime runtime;
};

PanelDeclaration declared(std::string name, PanelPlace where, std::string script, const std::filesystem::path& folder)
{
    const QString title = QString::fromStdString(name).replace(0, 1, QString::fromStdString(name).left(1).toUpper());
    return {.extension = kExtension.toStdString(),
            .name = std::move(name),
            .where = where,
            .script = std::move(script),
            .title = title,
            .icon = QStringLiteral("activity"),
            .extensionName = QStringLiteral("Stats tools"),
            .folder = folder};
}

gs::script::ServiceAnswer call(const Logic& logic, std::string_view service, std::string_view json)
{
    return LogicServices::instance().call(logic.runtime.caller(), service, json);
}

QVariantMap entryOf(const QVariantList& list, const QString& where, const QString& device)
{
    for (const QVariant& item : list) {
        const QVariantMap entry = item.toMap();
        if (entry.value(QStringLiteral("where")).toString() == where &&
            entry.value(QStringLiteral("device")).toString() == device) {
            return entry;
        }
    }
    return {};
}

template <typename Condition>
bool settle(Condition condition, std::chrono::milliseconds timeout = 3s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

std::string problemOf(std::string_view json, const std::filesystem::path& folder = {})
{
    return checkUiTree(json, folder).problem;
}

std::string chain(int levels)
{
    std::string json = R"({"type":"separator"})";
    for (int level = 1; level < levels; ++level) {
        json = R"({"type":"column","children":[)" + json + "]}";
    }
    return json;
}

std::string separators(int count)
{
    std::string json = R"({"type":"column","children":[)";
    for (int index = 0; index < count; ++index) {
        json += index == 0 ? R"({"type":"separator"})" : R"(,{"type":"separator"})";
    }
    return json + "]}";
}

std::string textRender(const std::string& text)
{
    return R"({"panel":"stats","tree":{"type":"text","text":")" + text + R"("}})";
}

QVariantMap childOf(const QVariantMap& node, int index)
{
    return node.value(QStringLiteral("children")).toList().value(index).toMap();
}

}

TEST_CASE("ADR-0105: checkUiTree takes every node type and fills in what a node leaves out")
{
    Folder folder;
    folder.write(QStringLiteral("pics/a.png"), 16);
    const UiCheck check = checkUiTree(R"({"type":"column","children":[
        {"type":"text","text":"Rounds **3**","format":"markdown"},
        {"type":"row","children":[
            {"type":"button","id":"go","title":"Go","icon":"play","style":"primary"},
            {"type":"toggle","id":"auto","title":"Auto","value":true}]},
        {"type":"group","title":"Settings","children":[
            {"type":"field","id":"name","title":"Name","value":"Ann","placeholder":"Your name"},
            {"type":"number","id":"below","max":-5},
            {"type":"choice","id":"mode","options":["easy",{"id":"hard","title":"Hard"}],"value":"hard"},
            {"type":"slider","id":"speed"}]},
        {"type":"list","id":"rounds","items":[{"id":"r1","title":"Round 1","subtitle":"Won","icon":"trophy"}],
         "selected":"r1"},
        {"type":"table","columns":["Name","Score","Won","Note"],"rows":[["Ann",3,true,null]]},
        {"type":"chart","kind":"bar","series":[{"title":"Score","color":"accent","points":[1,2,3]},
                                                {"color":"#22D3C5","points":[3,2]}]},
        {"type":"canvas","width":200,"height":100,"shapes":[
            {"kind":"rect","x":0,"y":0,"width":10,"height":10,"fill":"success"},
            {"kind":"circle","x":5,"y":5,"radius":2,"stroke":"#FF0000","strokeWidth":2},
            {"kind":"line","x1":0,"y1":0,"x2":1,"y2":1},
            {"kind":"polyline","points":[0,0,1,1,2,0],"closed":true},
            {"kind":"text","x":1,"y":1,"text":"Hi","size":"caption","color":"textSecondary"}]},
        {"type":"progress","title":"Loading"},
        {"type":"separator"},
        {"type":"image","path":"pics/a.png","title":"Logo","width":64}]})",
                                      folder.path());
    INFO(check.problem);
    REQUIRE(check.ok());
    const QVariantMap root = check.value.toMap();
    CHECK(root.value(QStringLiteral("type")).toString() == "column");
    CHECK(root.value(QStringLiteral("children")).toList().size() == 10);

    const QVariantMap text = childOf(root, 0);
    CHECK(text.value(QStringLiteral("format")).toString() == "markdown");
    CHECK(text.value(QStringLiteral("style")).toString() == "body");
    CHECK(text.value(QStringLiteral("tone")).toString() == "normal");

    const QVariantMap button = childOf(childOf(root, 1), 0);
    CHECK(button.value(QStringLiteral("id")).toString() == "go");
    CHECK(button.value(QStringLiteral("style")).toString() == "primary");
    CHECK(button.value(QStringLiteral("enabled")).toBool());
    CHECK(childOf(childOf(root, 1), 1).value(QStringLiteral("value")).toBool());

    const QVariantMap group = childOf(root, 2);
    CHECK(group.value(QStringLiteral("title")).toString() == "Settings");
    CHECK(childOf(group, 0).value(QStringLiteral("placeholder")).toString() == "Your name");
    const QVariantMap number = childOf(group, 1);
    CHECK(number.value(QStringLiteral("value")).toDouble() == -5.0);
    CHECK_FALSE(number.contains(QStringLiteral("min")));
    const QVariantList options = childOf(group, 2).value(QStringLiteral("options")).toList();
    REQUIRE(options.size() == 2);
    CHECK(options[0].toMap().value(QStringLiteral("title")).toString() == "easy");
    CHECK(options[1].toMap().value(QStringLiteral("title")).toString() == "Hard");
    const QVariantMap slider = childOf(group, 3);
    CHECK(slider.value(QStringLiteral("min")).toDouble() == 0.0);
    CHECK(slider.value(QStringLiteral("max")).toDouble() == 1.0);
    CHECK(slider.value(QStringLiteral("value")).toDouble() == 0.0);

    const QVariantMap list = childOf(root, 3);
    CHECK(list.value(QStringLiteral("selected")).toString() == "r1");
    CHECK(list.value(QStringLiteral("selectable")).toBool());
    CHECK(list.value(QStringLiteral("items")).toList().value(0).toMap().value(QStringLiteral("subtitle")).toString()
          == "Won");

    const QVariantMap table = childOf(root, 4);
    CHECK(table.value(QStringLiteral("columns")).toStringList().size() == 4);
    CHECK(table.value(QStringLiteral("rows")).toList().value(0).toStringList()
          == QStringList{QStringLiteral("Ann"), QStringLiteral("3"), QStringLiteral("true"), QString()});

    const QVariantList series = childOf(root, 5).value(QStringLiteral("series")).toList();
    REQUIRE(series.size() == 2);
    CHECK(series[0].toMap().value(QStringLiteral("color")).toString() == "accent");
    CHECK(series[0].toMap().value(QStringLiteral("points")).toList().size() == 3);

    const QVariantList shapes = childOf(root, 6).value(QStringLiteral("shapes")).toList();
    REQUIRE(shapes.size() == 5);
    CHECK(shapes[0].toMap().value(QStringLiteral("fill")).toString() == "success");
    CHECK(shapes[0].toMap().value(QStringLiteral("strokeWidth")).toDouble() == 1.0);
    CHECK(shapes[3].toMap().value(QStringLiteral("closed")).toBool());
    CHECK(shapes[4].toMap().value(QStringLiteral("size")).toString() == "caption");

    CHECK(childOf(root, 7).value(QStringLiteral("value")).toDouble() == -1.0);
    CHECK(childOf(root, 8).value(QStringLiteral("type")).toString() == "separator");
    const QString source = childOf(root, 9).value(QStringLiteral("source")).toString();
    CHECK(source.startsWith(QStringLiteral("file:///")));
    CHECK(source.endsWith(QStringLiteral("/pics/a.png")));
}

TEST_CASE("ADR-0105: checkUiTree refuses a node that is not right and names it by its path")
{
    struct Case {
        std::string_view json;
        std::string_view problem;
    };
    const Case cases[] = {
        {"not json", "the tree is not valid JSON"},
        {"[1]", "a node must be an object at $"},
        {R"({"text":"x"})", "a node needs a type at $"},
        {R"({"type":"video"})", "\"video\" is not a node type at $.type"},
        {R"({"type":"button","title":"Go"})", "a button needs an id, so that its events say which one it was at $"},
        {R"({"type":"button","id":"go"})", "title is missing at $"},
        {R"({"type":"button","id":"go","title":"Go","style":"ghost"})",
         "style must be normal, primary or danger at $.style"},
        {R"({"type":"text","text":3})", "text must be text at $.text"},
        {R"({"type":"text","text":"x","tone":"loud"})", "tone must be normal, muted, accent, good, warn or bad at $.tone"},
        {R"({"type":"column","children":{}})", "children must be a list at $.children"},
        {R"({"type":"column","children":[{"type":"text","text":"a"},{"type":"slider","id":"s","value":2}]})",
         "value is outside min and max at $.children[1].value"},
        {R"({"type":"choice","id":"c","options":["a","b"],"value":"z"})",
         "value is not the id of one of the options at $.value"},
        {R"({"type":"choice","id":"c","options":[]})", "options needs at least one option at $.options"},
        {R"({"type":"number","id":"n","min":5,"max":1})", "min is above max at $.min"},
        {R"({"type":"number","id":"n","step":0})", "step must be more than 0 at $.step"},
        {R"({"type":"toggle","id":"t","title":"T","value":"yes"})", "value must be true or false at $.value"},
        {R"({"type":"list","id":"l","items":[{"title":"No id"}]})", "id is missing at $.items[0]"},
        {R"({"type":"table","columns":["a"],"rows":[["x","y"]]})",
         "a row must be a list of at most one cell a column at $.rows[0]"},
        {R"({"type":"table","columns":["a"],"rows":[[{"x":1}]]})",
         "a cell must be text of at most 200 characters, a number, true, false or null at $.rows[0][0]"},
        {R"({"type":"chart","height":50,"series":[]})", "height must be from 60 to 480 at $.height"},
        {R"({"type":"chart","series":[{"points":[1,"2"]}]})", "a point must be a number at $.series[0].points[1]"},
        {R"({"type":"canvas","width":0,"height":10})", "width must be from 1 to 2000 at $.width"},
        {R"({"type":"canvas","width":10,"height":10,"shapes":[{"kind":"star"}]})",
         "kind must be rect, circle, line, polyline or text at $.shapes[0].kind"},
        {R"({"type":"canvas","width":10,"height":10,"shapes":[{"kind":"rect","x":0,"y":0,"width":1,"height":1,"fill":"red"}]})",
         "fill must be a color written #RRGGBB or the name of a theme color at $.shapes[0].fill"},
        {R"({"type":"canvas","width":10,"height":10,"shapes":[{"kind":"polyline","points":[0,0,1]}]})",
         "points must hold x and y of at least two points at $.shapes[0].points"},
        {R"({"type":"progress","value":2})", "value must be from 0 to 1, or null while it is not known at $.value"},
    };
    for (const Case& each : cases) {
        CAPTURE(each.json);
        CHECK(problemOf(each.json) == each.problem);
    }
}

TEST_CASE("ADR-0105: a tree is at most 256 KiB as JSON, 2000 nodes and 16 levels deep")
{
    CHECK(checkUiTree(chain(16), {}).ok());
    std::string deepest = "$";
    for (int level = 1; level < 17; ++level) {
        deepest += ".children[0]";
    }
    CHECK(problemOf(chain(17)) == "the tree is deeper than 16 levels at " + deepest);
    CHECK(problemOf(chain(26)) == "the tree is nested deeper than 16 levels");

    CHECK(checkUiTree(separators(1999), {}).ok());
    CHECK(problemOf(separators(2000)) == "there are more than 2000 nodes at $.children[1999]");

    const std::string huge = R"({"type":"text","text":")" + std::string(kMostUiTreeBytes, 'a') + R"("})";
    CHECK(problemOf(huge) == "the tree is larger than 256 KiB as JSON");
    const std::string wordy = R"({"type":"text","text":")" + std::string(4001, 'a') + R"("})";
    CHECK(problemOf(wordy) == "text is longer than 4000 characters at $.text");
}

TEST_CASE("ADR-0105: a picture is a PNG or SVG of at most 2 MiB inside the extension's folder")
{
    Folder folder;
    folder.write(QStringLiteral("pics/a.png"), 16);
    folder.write(QStringLiteral("pics/b.SVG"), 16);
    folder.write(QStringLiteral("pics/a.jpg"), 16);
    folder.write(QStringLiteral("pics/big.png"), static_cast<qint64>(kMostUiImageBytes) + 1);
    folder.folder(QStringLiteral("pics/dir.png"));
    const auto image = [](std::string_view path) {
        return R"({"type":"image","path":")" + std::string(path) + R"("})";
    };
    CHECK(checkUiTree(image("pics/a.png"), folder.path()).ok());
    CHECK(checkUiTree(image("pics/b.SVG"), folder.path()).ok());
    const std::string outside = "the picture must be a file inside the extension's folder at $.path";
    for (const std::string_view path : {"../outside.png", "C:/Windows/win.png", "/pics/a.png", R"(pics\\a.png)",
                                        "pics/../../a.png"}) {
        CAPTURE(path);
        CHECK(problemOf(image(path), folder.path()) == outside);
    }
    CHECK(problemOf(image("pics/a.jpg"), folder.path()) == "the picture must be a .png or .svg file at $.path");
    CHECK(problemOf(image("pics/missing.png"), folder.path())
          == "there is no picture at this path in the extension's folder at $.path");
    CHECK(problemOf(image("pics/dir.png"), folder.path())
          == "there is no picture at this path in the extension's folder at $.path");
    CHECK(problemOf(image("pics/big.png"), folder.path()) == "the picture is larger than 2 MiB at $.path");
    CHECK(problemOf(image("pics/a.png"))
          == "pictures come from the extension's folder, and this logic has none at $.path");
    CHECK(problemOf(R"({"type":"image"})", folder.path()) == "path is missing at $");
}

TEST_CASE("ADR-0105: status widgets show a small row of text, icons and progress, and menus up to 16 items")
{
    const UiCheck status = checkStatusTree(
        R"({"type":"row","children":[{"type":"icon","name":"gauge","tone":"good"},{"type":"text","text":"60 fps"},
                                     {"type":"progress","value":0.5}]})");
    INFO(status.problem);
    REQUIRE(status.ok());
    CHECK(childOf(status.value.toMap(), 0).value(QStringLiteral("tone")).toString() == "good");
    CHECK(childOf(status.value.toMap(), 2).value(QStringLiteral("value")).toDouble() == 0.5);
    CHECK(checkStatusTree(R"({"type":"row","children":[{"type":"row"}]})").problem
          == "a row in a status widget cannot hold another row at $.children[0]");
    CHECK(checkStatusTree(R"({"type":"button","id":"b","title":"B"})").problem
          == "a status widget shows only row, text, icon and progress at $.type");
    CHECK(checkStatusTree(R"({"type":"text","text":")" + std::string(101, 'a') + R"("})").problem
          == "text is longer than 100 characters at $.text");
    CHECK(checkStatusTree(R"({"type":"icon"})").problem == "name is missing at $");
    std::string row = R"({"type":"row","children":[)";
    for (int index = 0; index < 64; ++index) {
        row += index == 0 ? R"({"type":"text","text":"x"})" : R"(,{"type":"text","text":"x"})";
    }
    CHECK(checkStatusTree(row + "]}").problem == "there are more than 64 nodes at $.children[63]");

    const UiCheck menu = checkMenuItems(
        R"([{"id":"reset","title":"Reset","icon":"rotate-ccw"},{"id":"mute","title":"Mute","checked":true,"enabled":false}])");
    INFO(menu.problem);
    REQUIRE(menu.ok());
    const QVariantList items = menu.value.toList();
    REQUIRE(items.size() == 2);
    CHECK_FALSE(items[0].toMap().value(QStringLiteral("checkable")).toBool());
    CHECK(items[0].toMap().value(QStringLiteral("enabled")).toBool());
    CHECK(items[1].toMap().value(QStringLiteral("checkable")).toBool());
    CHECK(items[1].toMap().value(QStringLiteral("checked")).toBool());
    CHECK_FALSE(items[1].toMap().value(QStringLiteral("enabled")).toBool());
    CHECK(checkMenuItems(R"({"id":"x"})").problem == "the menu items must be a list");
    std::string many = "[";
    for (int index = 0; index < 17; ++index) {
        many += (index == 0 ? "" : ",") + std::string(R"({"id":"i","title":"I"})");
    }
    CHECK(checkMenuItems(many + "]").problem == "the menu has more than 16 items");
    CHECK(checkMenuItems(R"([{"id":"a"}])").problem == "title is missing at $[0]");
    CHECK(checkMenuItems(R"([{"id":"a","title":"A"},3])").problem == "a menu item must be an object at $[1]");
    CHECK(checkMenuItems(R"([{"id":"a","title":"A","checked":"yes"}])").problem
          == "checked must be true or false at $[0].checked");
}

TEST_CASE("ADR-0105: each phone's logic draws its own panels, and the user's events go back to that logic")
{
    Application application;
    Folder folder;
    ExtensionPanels panels(nullptr);
    panels.setDeclarations({declared("stats", PanelPlace::Mirror, "session", folder.path()),
                            declared("menu", PanelPlace::Menu, "session", folder.path()),
                            declared("fps", PanelPlace::Status, "session", folder.path()),
                            declared("history", PanelPlace::Page, "app", folder.path())});
    struct Told {
        std::uint64_t logic = 0;
        QString kind;
        QString id;
        QJsonObject event;
    };
    std::vector<Told> told;
    panels.setTeller([&told](std::uint64_t logic, const QString& kind, const QString& id, const QString& json) {
        told.push_back({logic, kind, id, QJsonDocument::fromJson(json.toUtf8()).object()});
        return true;
    });
    QStringList changed;
    QObject::connect(&panels, &ExtensionPanels::treeChanged, [&changed](const QString& key) { changed.append(key); });
    QStringList closed;
    QObject::connect(&panels, &ExtensionPanels::closeRequested, [&closed](const QString& key) { closed.append(key); });

    Logic phoneA(kExtension, QStringLiteral("session"), QStringLiteral("phone-a"), folder.path());
    auto phoneB = std::make_unique<Logic>(kExtension, QStringLiteral("session"), QStringLiteral("phone-b"),
                                          folder.path());
    Logic app(kExtension, QStringLiteral("app"), QString(), folder.path());
    Logic user(QString(), QStringLiteral("session"), QStringLiteral("phone-a"), folder.path());
    const std::uint64_t logicA = phoneA.runtime.caller().logic;
    const std::uint64_t logicB = phoneB->runtime.caller().logic;

    CHECK(panels.panels().size() == 3);
    CHECK(panels.menus().isEmpty());
    const QVariantMap mirrorA = entryOf(panels.panels(), QStringLiteral("mirror"), QStringLiteral("phone-a"));
    const QVariantMap mirrorB = entryOf(panels.panels(), QStringLiteral("mirror"), QStringLiteral("phone-b"));
    const QVariantMap page = entryOf(panels.panels(), QStringLiteral("page"), QString());
    REQUIRE_FALSE(mirrorA.isEmpty());
    REQUIRE_FALSE(mirrorB.isEmpty());
    REQUIRE_FALSE(page.isEmpty());
    const QString keyA = mirrorA.value(QStringLiteral("key")).toString();
    const QString keyB = mirrorB.value(QStringLiteral("key")).toString();
    const QString pageKey = page.value(QStringLiteral("key")).toString();
    CHECK(keyA == QStringLiteral("p/%1/stats").arg(logicA));
    CHECK(keyA != keyB);
    CHECK(mirrorA.value(QStringLiteral("id")).toString() == "alice.stats/stats");
    CHECK(mirrorA.value(QStringLiteral("title")).toString() == "Stats");
    CHECK(mirrorA.value(QStringLiteral("extensionName")).toString() == "Stats tools");
    CHECK(mirrorA.value(QStringLiteral("icon")).toString() == "activity");
    CHECK(mirrorA.value(QStringLiteral("logic")).toULongLong() == logicA);
    CHECK(page.value(QStringLiteral("title")).toString() == "History");
    CHECK_FALSE(panels.tree(keyA).isValid());

    REQUIRE(call(phoneA, "ui.render", textRender("A")).code.empty());
    REQUIRE(settle([&] { return panels.tree(keyA).isValid(); }));
    CHECK(panels.tree(keyA).toMap().value(QStringLiteral("text")).toString() == "A");
    CHECK_FALSE(panels.tree(keyB).isValid());
    CHECK(changed.contains(keyA));
    CHECK_FALSE(changed.contains(keyB));

    REQUIRE(call(*phoneB, "ui.status", R"({"name":"fps","tree":{"type":"text","text":"60"}})").code.empty());
    REQUIRE(settle([&] {
        return !entryOf(panels.panels(), QStringLiteral("status"), QStringLiteral("phone-b")).isEmpty();
    }));
    const QString statusKey = entryOf(panels.panels(), QStringLiteral("status"), QStringLiteral("phone-b"))
                                  .value(QStringLiteral("key"))
                                  .toString();
    CHECK(statusKey.startsWith(QStringLiteral("s/")));
    CHECK(panels.tree(statusKey).toMap().value(QStringLiteral("text")).toString() == "60");
    CHECK(entryOf(panels.panels(), QStringLiteral("status"), QStringLiteral("phone-a")).isEmpty());

    REQUIRE(call(phoneA, "ui.menu",
                 R"({"items":[{"id":"reset","title":"Reset"},{"id":"mute","title":"Mute","checked":false}]})")
                .code.empty());
    REQUIRE(settle([&] { return panels.menus().size() == 1; }));
    const QVariantMap group = panels.menus().front().toMap();
    CHECK(group.value(QStringLiteral("device")).toString() == "phone-a");
    CHECK(group.value(QStringLiteral("where")).toString() == "menu");
    const QVariantList items = group.value(QStringLiteral("items")).toList();
    REQUIRE(items.size() == 2);
    CHECK(items[1].toMap().value(QStringLiteral("checkable")).toBool());

    CHECK(call(user, "ui.render", textRender("U")).code == "GS-API-076");
    CHECK(call(phoneA, "ui.render", R"({"panel":"history","tree":null})").code == "GS-API-076");
    CHECK(call(app, "ui.render", R"({"panel":"stats","tree":null})").code == "GS-API-076");
    CHECK(call(phoneA, "ui.render", R"({"panel":"nope","tree":null})").code == "GS-API-076");
    CHECK(call(phoneA, "ui.status", R"({"name":"stats","tree":null})").code == "GS-API-076");
    CHECK(call(app, "ui.menu", R"({"items":[]})").code == "GS-API-076");
    CHECK(call(phoneA, "ui.close", R"({"panel":"fps"})").code == "GS-API-076");
    const gs::script::ServiceAnswer bad =
        call(phoneA, "ui.render", R"({"panel":"stats","tree":{"type":"button","id":"go"}})");
    CHECK(bad.code == "GS-API-075");
    CHECK(bad.problem == "title is missing at $");
    CHECK(call(phoneA, "ui.render", "[]").code == "GS-API-075");
    CHECK(call(phoneA, "ui.render", R"({"panel":"stats"})").code == "GS-API-075");
    CHECK(call(phoneA, "ui.menu", R"({"items":[{"id":"x"}]})").code == "GS-API-075");

    REQUIRE(call(app, "ui.render", R"({"panel":"history","tree":{"type":"text","text":"H"}})").code.empty());
    REQUIRE(settle([&] { return panels.tree(pageKey).isValid(); }));
    REQUIRE(call(phoneA, "ui.render", R"({"panel":"stats","tree":null})").code.empty());
    REQUIRE(settle([&] { return panels.tree(keyA).toMap().value(QStringLiteral("type")).toString() == "column"; }));
    CHECK(panels.tree(keyA).toMap().value(QStringLiteral("children")).toList().isEmpty());

    CHECK(panels.send(logicA, QStringLiteral("stats"), QStringLiteral("click"), QStringLiteral("go"), QVariant()));
    CHECK(panels.send(logicB, QStringLiteral("menu"), QStringLiteral("select"), QStringLiteral("mute"), true));
    CHECK_FALSE(panels.send(logicA, QStringLiteral("stats"), QStringLiteral("hover"), QStringLiteral("go"), QVariant()));
    CHECK_FALSE(panels.send(logicA, QString(), QStringLiteral("click"), QStringLiteral("go"), QVariant()));
    REQUIRE(told.size() == 2);
    CHECK(told[0].logic == logicA);
    CHECK(told[0].kind == "ui");
    CHECK(told[0].id == "stats");
    CHECK(told[0].event.value(QStringLiteral("type")).toString() == "click");
    CHECK(told[0].event.value(QStringLiteral("id")).toString() == "go");
    CHECK(told[0].event.value(QStringLiteral("value")).isNull());
    CHECK(told[1].logic == logicB);
    CHECK(told[1].id == "menu");
    CHECK(told[1].event.value(QStringLiteral("value")).toBool());

    REQUIRE(call(phoneA, "ui.close", R"({"panel":"stats"})").code.empty());
    REQUIRE(settle([&] { return closed.contains(keyA); }));

    REQUIRE(call(*phoneB, "ui.render", textRender("B")).code.empty());
    REQUIRE(settle([&] { return panels.tree(keyB).isValid(); }));
    changed.clear();
    phoneB.reset();
    CHECK(entryOf(panels.panels(), QStringLiteral("mirror"), QStringLiteral("phone-b")).isEmpty());
    CHECK(entryOf(panels.panels(), QStringLiteral("status"), QStringLiteral("phone-b")).isEmpty());
    CHECK_FALSE(entryOf(panels.panels(), QStringLiteral("mirror"), QStringLiteral("phone-a")).isEmpty());
    CHECK_FALSE(panels.tree(keyB).isValid());
    CHECK_FALSE(panels.tree(statusKey).isValid());
    CHECK(changed.contains(keyB));
    CHECK(call(phoneA, "ui.render", textRender("still here")).code.empty());

    panels.setSideState(QStringLiteral("phone-a"), QStringLiteral("alice.stats/stats"), true, 300);
    const QVariantMap side = panels.sideState(QStringLiteral("phone-a"));
    CHECK(side.value(QStringLiteral("panel")).toString() == "alice.stats/stats");
    CHECK(side.value(QStringLiteral("open")).toBool());
    CHECK(side.value(QStringLiteral("width")).toInt() == 300);
    CHECK_FALSE(panels.sideState(QStringLiteral("phone-z")).value(QStringLiteral("open")).toBool());
    CHECK(panels.iconOf(QString()).isEmpty());
    CHECK(panels.iconOf(QStringLiteral("activity")) == "activity");
}

TEST_CASE("ADR-0105: renders quicker than 30 a second are merged, and the last tree shows")
{
    Application application;
    Folder folder;
    ExtensionPanels panels(nullptr);
    panels.setDeclarations({declared("stats", PanelPlace::Mirror, "session", folder.path())});
    int changes = 0;
    QString key;
    QObject::connect(&panels, &ExtensionPanels::treeChanged, [&changes, &key](const QString& changedKey) {
        if (changedKey == key) {
            ++changes;
        }
    });
    Logic phone(kExtension, QStringLiteral("session"), QStringLiteral("phone-a"), folder.path());
    key = entryOf(panels.panels(), QStringLiteral("mirror"), QStringLiteral("phone-a")).value(QStringLiteral("key")).toString();
    REQUIRE_FALSE(key.isEmpty());
    const auto shown = [&panels, &key] {
        return panels.tree(key).toMap().value(QStringLiteral("text")).toString();
    };

    for (int round = 0; round < 100; ++round) {
        REQUIRE(call(phone, "ui.render", textRender("burst " + std::to_string(round))).code.empty());
    }
    REQUIRE(settle([&] { return shown() == "burst 99"; }));
    CHECK(changes == 1);

    changes = 0;
    const auto started = std::chrono::steady_clock::now();
    for (int round = 0; round < 20; ++round) {
        REQUIRE(call(phone, "ui.render", textRender("step " + std::to_string(round))).code.empty());
        QCoreApplication::processEvents();
    }
    const auto spent = std::chrono::steady_clock::now() - started;
    REQUIRE(settle([&] { return shown() == "step 19"; }));
    CHECK(changes <= spent / kUiRenderGap + 2);
    CHECK(changes < 20);
}

TEST_CASE("ADR-0105: QML hands out the panels the program made at start instead of making its own")
{
    Application application;
    Folder folder;
    ExtensionPanels panels(nullptr);
    panels.setDeclarations({declared("stats", PanelPlace::Mirror, "session", folder.path())});
    qmlRegisterTypesAndRevisions<ExtensionPanels>("GameScrcpy.PanelsTest", 1);
    QQmlEngine engine;
    CHECK(engine.singletonInstance<ExtensionPanels*>("GameScrcpy.PanelsTest", "ExtensionPanels") == &panels);
    CHECK(ExtensionPanels::instance() == &panels);
    Logic phone(kExtension, QStringLiteral("session"), QStringLiteral("phone-a"), folder.path());
    CHECK(call(phone, "ui.render", textRender("still answered")).code.empty());
}

TEST_CASE("ADR-0112: panels show device items, steps, QR codes and rows of names and values; a toolbar button "
          "takes a title, an icon and its state")
{
    Folder folder;
    folder.text(QStringLiteral("icons/box.svg"), R"(<svg viewBox="0 0 24 24"><path d="M2 2 L22 22"/></svg>)");
    const UiCheck check = checkUiTree(
        R"({"type":"column","children":[)"
        R"({"type":"deviceItem","id":"box-1","title":"Box","subtitle":"USB","icon":"svg:icons/box.svg","state":"busy",)"
        R"("stateText":"Flashing"},)"
        R"({"type":"deviceItem","title":"Plain","icon":"cpu"},)"
        R"({"type":"steps","steps":["Plug in","Flash","Done"],"current":1,"failed":true},)"
        R"({"type":"qr","text":"https://example.com/pair","title":"Scan"},)"
        R"({"type":"keyValue","rows":[{"key":"Port","value":"COM9"},{"key":"Baud","value":2000000},)"
        R"({"key":"Ready","value":true},{"key":"Note"}]}]})",
        folder.path());
    REQUIRE_MESSAGE(check.ok(), check.problem);
    const QVariantMap tree = check.value.toMap();
    const QVariantMap device = childOf(tree, 0);
    CHECK(device.value(QStringLiteral("iconPath")).toString() == "M2 2 L22 22");
    CHECK(device.value(QStringLiteral("icon")).toString().isEmpty());
    CHECK(device.value(QStringLiteral("subtitle")).toString() == "USB");
    CHECK(device.value(QStringLiteral("state")).toString() == "busy");
    CHECK(device.value(QStringLiteral("stateText")).toString() == "Flashing");
    const QVariantMap plain = childOf(tree, 1);
    CHECK(plain.value(QStringLiteral("state")).toString().isEmpty());
    CHECK(plain.value(QStringLiteral("icon")).toString() == "cpu");
    CHECK(plain.value(QStringLiteral("iconPath")).toString().isEmpty());
    const QVariantMap steps = childOf(tree, 2);
    CHECK(steps.value(QStringLiteral("steps")).toStringList() == QStringList{"Plug in", "Flash", "Done"});
    CHECK(steps.value(QStringLiteral("current")).toInt() == 1);
    CHECK(steps.value(QStringLiteral("failed")).toBool());
    const QVariantMap qr = childOf(tree, 3);
    CHECK(qr.value(QStringLiteral("modules")).toInt() >= 21);
    CHECK(qr.value(QStringLiteral("path")).toString().startsWith(u'M'));
    CHECK(qr.value(QStringLiteral("title")).toString() == "Scan");
    const QVariantList rows = childOf(tree, 4).value(QStringLiteral("rows")).toList();
    REQUIRE(rows.size() == 4);
    CHECK(rows[0].toMap().value(QStringLiteral("value")).toString() == "COM9");
    CHECK(rows[1].toMap().value(QStringLiteral("value")).toString() == "2000000");
    CHECK(rows[2].toMap().value(QStringLiteral("value")).toString() == "true");
    CHECK(rows[3].toMap().value(QStringLiteral("key")).toString() == "Note");
    CHECK(rows[3].toMap().value(QStringLiteral("value")).toString().isEmpty());

    CHECK(problemOf(R"({"type":"deviceItem"})") == "title is missing at $");
    CHECK(problemOf(R"({"type":"deviceItem","title":"B","state":"gone"})") ==
          "state must be ready, busy, problem or offline at $.state");
    CHECK(problemOf(R"({"type":"deviceItem","title":"B","icon":"svg:icons/box.svg"})") ==
          "an svg: icon comes from the extension's folder, and this logic has none at $.icon");
    CHECK(problemOf(R"({"type":"deviceItem","title":"B","icon":"svg:icons/none.svg"})", folder.path()) ==
          "the icon must be an SVG of plain shapes on the 24 grid inside the extension's folder at $.icon");
    CHECK(problemOf(R"({"type":"steps","steps":["One"]})") == "steps needs at least 2 steps at $.steps");
    CHECK(problemOf(R"({"type":"steps","steps":["A",""]})") == "a step must be text of 1 to 40 characters at $.steps[1]");
    CHECK(problemOf(R"({"type":"steps","steps":["A","B"],"current":3})") ==
          "current must be a whole number from 0 to the number of steps at $.current");
    CHECK(problemOf(R"({"type":"steps","steps":["A","B"],"current":0.5})") ==
          "current must be a whole number from 0 to the number of steps at $.current");
    CHECK(problemOf(R"({"type":"steps","steps":["A","B"],"current":2})").empty());
    CHECK(problemOf(R"({"type":"qr","text":""})") == "text is empty at $.text");
    std::string wide;
    for (int index = 0; index < 400; ++index) {
        wide += "\xE4\xB8\xAD";
    }
    CHECK(problemOf(R"({"type":"qr","text":")" + wide + R"("})") == "text is longer than 1000 bytes as UTF-8 at $.text");
    CHECK(problemOf(R"({"type":"keyValue","rows":[{"value":1}]})") == "key is missing at $.rows[0]");
    CHECK(problemOf(R"({"type":"keyValue","rows":[{"key":"A","value":[1]}]})") ==
          "value must be text of at most 200 characters, a number, true, false or null at $.rows[0].value");
    CHECK(problemOf(R"({"type":"keyValue"})") == "rows is missing at $");

    const UiCheck state = checkToolbarState(R"({"title":"Rec","icon":"svg:icons/box.svg","checked":true})", folder.path());
    REQUIRE_MESSAGE(state.ok(), state.problem);
    CHECK(state.value.toMap().value(QStringLiteral("iconPath")).toString() == "M2 2 L22 22");
    CHECK(state.value.toMap().value(QStringLiteral("checked")).toBool());
    CHECK(state.value.toMap().value(QStringLiteral("enabled")).toBool());
    CHECK(checkToolbarState("[]", folder.path()).problem == "a toolbar button is set with an object");
    CHECK(checkToolbarState(R"({"checked":"yes"})", folder.path()).problem == "checked must be true or false at $.checked");
    CHECK(checkToolbarState(R"({"title":")" + std::string(41, 'x') + R"("})", folder.path()).problem ==
          "title is longer than 40 characters at $.title");
}

TEST_CASE("ADR-0112: the app logic draws panels for Settings and the extension's settings window and fills card "
          "menus and tray groups by name; a phone's logic sets its toolbar button; choosing an item says on which "
          "card")
{
    Application application;
    Folder folder;
    ExtensionPanels panels(nullptr);
    std::vector<PanelDeclaration> declarations{declared("options", PanelPlace::Settings, "app", folder.path()),
                                               declared("main", PanelPlace::Extension, "app", folder.path()),
                                               declared("quick", PanelPlace::Toolbar, "session", folder.path()),
                                               declared("more", PanelPlace::ContextMenu, "app", folder.path()),
                                               declared("tray", PanelPlace::Tray, "app", folder.path())};
    declarations[2].iconPath = QStringLiteral("M2 2 L22 22");
    declarations[3].on = {"device", "profile"};
    panels.setDeclarations(declarations);
    struct Told {
        std::uint64_t logic = 0;
        QString id;
        QJsonObject event;
    };
    std::vector<Told> told;
    panels.setTeller([&told](std::uint64_t logic, const QString&, const QString& id, const QString& json) {
        told.push_back({logic, id, QJsonDocument::fromJson(json.toUtf8()).object()});
        return true;
    });
    Logic app(kExtension, QStringLiteral("app"), QString(), folder.path());
    Logic phone(kExtension, QStringLiteral("session"), QStringLiteral("phone-a"), folder.path());
    const std::uint64_t appLogic = app.runtime.caller().logic;

    const QVariantMap settings = entryOf(panels.panels(), QStringLiteral("settings"), QString());
    const QVariantMap window = entryOf(panels.panels(), QStringLiteral("extension"), QString());
    const auto button = [&panels] { return entryOf(panels.panels(), QStringLiteral("toolbar"), QStringLiteral("phone-a")); };
    REQUIRE_FALSE(settings.isEmpty());
    REQUIRE_FALSE(window.isEmpty());
    REQUIRE_FALSE(button().isEmpty());
    CHECK(entryOf(panels.panels(), QStringLiteral("toolbar"), QString()).isEmpty());
    CHECK(button().value(QStringLiteral("iconPath")).toString() == "M2 2 L22 22");
    CHECK(button().value(QStringLiteral("toolbar")).toMap().isEmpty());
    CHECK(panels.menus().isEmpty());

    REQUIRE(call(app, "ui.render", R"({"panel":"options","tree":{"type":"text","text":"O"}})").code.empty());
    REQUIRE(settle([&] { return panels.tree(settings.value(QStringLiteral("key")).toString()).isValid(); }));
    REQUIRE(call(app, "ui.render", R"({"panel":"main","tree":{"type":"text","text":"M"}})").code.empty());
    CHECK(call(phone, "ui.render", R"({"panel":"options","tree":null})").code == "GS-API-076");

    REQUIRE(call(phone, "ui.toolbar", R"({"name":"quick","state":{"title":"Rec","checked":true}})").code.empty());
    REQUIRE(settle([&] { return button().value(QStringLiteral("toolbar")).toMap().value(QStringLiteral("checked")).toBool(); }));
    CHECK(button().value(QStringLiteral("toolbar")).toMap().value(QStringLiteral("title")).toString() == "Rec");
    REQUIRE(call(phone, "ui.render", R"({"panel":"quick","tree":{"type":"text","text":"Q"}})").code.empty());
    CHECK(call(phone, "ui.toolbar", R"({"name":"quick","state":{"checked":"yes"}})").code == "GS-API-075");
    CHECK(call(app, "ui.toolbar", R"({"name":"quick","state":null})").code == "GS-API-076");
    CHECK(call(phone, "ui.toolbar", R"({"name":"more","state":null})").code == "GS-API-076");
    CHECK(call(phone, "ui.status", R"({"name":"quick","tree":null})").code == "GS-API-076");
    REQUIRE(call(phone, "ui.toolbar", R"({"name":"quick","state":null})").code.empty());
    REQUIRE(settle([&] { return button().value(QStringLiteral("toolbar")).toMap().isEmpty(); }));

    REQUIRE(call(app, "ui.contextMenu", R"({"name":"more","items":[{"id":"copy","title":"Copy"}]})").code.empty());
    REQUIRE(settle([&] { return panels.menus().size() == 1; }));
    const QVariantMap group = panels.menus().front().toMap();
    CHECK(group.value(QStringLiteral("where")).toString() == "contextMenu");
    CHECK(group.value(QStringLiteral("on")).toStringList() == QStringList{"device", "profile"});
    CHECK(group.value(QStringLiteral("device")).toString().isEmpty());
    CHECK(call(app, "ui.tray", R"({"name":"more","items":[]})").code == "GS-API-076");
    CHECK(call(app, "ui.contextMenu", R"({"name":"tray","items":[]})").code == "GS-API-076");
    CHECK(call(app, "ui.contextMenu", R"({"name":"options","items":[]})").code == "GS-API-076");
    CHECK(call(app, "ui.menu", R"({"items":[]})").code == "GS-API-076");
    CHECK(call(phone, "ui.contextMenu", R"({"name":"more","items":[]})").code == "GS-API-076");
    CHECK(call(app, "ui.contextMenu", R"({"name":"more","items":[{"id":"x"}]})").code == "GS-API-075");
    CHECK(call(app, "ui.contextMenu", R"({"items":[]})").code == "GS-API-075");

    REQUIRE(call(app, "ui.tray", R"({"name":"tray","items":[{"id":"pause","title":"Pause","checked":false}]})")
                .code.empty());
    REQUIRE(settle([&] { return panels.menus().size() == 2; }));
    CHECK(std::ranges::any_of(panels.menus(), [](const QVariant& each) {
        return each.toMap().value(QStringLiteral("where")).toString() == "tray";
    }));

    CHECK(panels.choose(appLogic, QStringLiteral("more"), QStringLiteral("copy"), QVariant(),
                        {{QStringLiteral("kind"), QStringLiteral("device")},
                         {QStringLiteral("id"), QStringLiteral("phone-a")},
                         {QStringLiteral("name"), QStringLiteral("Pixel")}}));
    CHECK(panels.choose(appLogic, QStringLiteral("tray"), QStringLiteral("pause"), true, {}));
    CHECK_FALSE(panels.choose(appLogic, QString(), QStringLiteral("copy"), QVariant(), {}));
    CHECK_FALSE(panels.choose(appLogic, QStringLiteral("more"), QString(), QVariant(), {}));
    REQUIRE(told.size() == 2);
    CHECK(told[0].logic == appLogic);
    CHECK(told[0].id == "more");
    CHECK(told[0].event.value(QStringLiteral("type")).toString() == "choose");
    CHECK(told[0].event.value(QStringLiteral("id")).toString() == "copy");
    CHECK(told[0].event.value(QStringLiteral("value")).isNull());
    const QJsonObject target = told[0].event.value(QStringLiteral("target")).toObject();
    CHECK(target.value(QStringLiteral("kind")).toString() == "device");
    CHECK(target.value(QStringLiteral("id")).toString() == "phone-a");
    CHECK(target.value(QStringLiteral("name")).toString() == "Pixel");
    CHECK(told[1].event.value(QStringLiteral("value")).toBool());
    CHECK_FALSE(told[1].event.contains(QStringLiteral("target")));

    REQUIRE(call(app, "ui.contextMenu", R"({"name":"more","items":null})").code.empty());
    REQUIRE(settle([&] { return panels.menus().size() == 1; }));
}
