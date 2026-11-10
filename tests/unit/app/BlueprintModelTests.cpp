#include "BlueprintModel.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariant>

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

using gs::app::BlueprintModel;
using gs::app::TextChange;

namespace {

class FakeText final : public QObject, public gs::app::BlueprintText
{
    Q_OBJECT
    Q_INTERFACES(gs::app::BlueprintText)
    Q_PROPERTY(QString path READ path CONSTANT FINAL)
    Q_PROPERTY(QString name READ name CONSTANT FINAL)
    Q_PROPERTY(bool readOnly READ readOnly CONSTANT FINAL)

public:
    explicit FakeText(QString text, QString path = QStringLiteral("logic.ts"))
        : m_text(std::move(text))
        , m_path(std::move(path))
    {
    }

    [[nodiscard]] QString text() const override { return m_text; }
    bool edit(std::span<const TextChange> changes) override
    {
        std::vector<TextChange> ordered(changes.begin(), changes.end());
        std::ranges::stable_sort(ordered, {}, &TextChange::start);
        for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
            m_text.replace(it->start, it->end - it->start, it->text);
        }
        ++m_edits;
        emit textChanged();
        return true;
    }
    [[nodiscard]] QTextDocument* editor() const override { return nullptr; }
    void setLayoutDirty(bool dirty) override { m_layoutDirty = dirty; }
    [[nodiscard]] bool layoutDirty() const noexcept { return m_layoutDirty; }
    [[nodiscard]] QString path() const { return m_path; }
    [[nodiscard]] QString name() const { return m_path.section(u'/', -1); }
    [[nodiscard]] bool readOnly() const { return false; }
    [[nodiscard]] int edits() const noexcept { return m_edits; }

    void type(const QString& text)
    {
        m_text = text;
        emit textChanged();
    }

signals:
    void textChanged();
    void saved();

private:
    QString m_text;
    QString m_path;
    int m_edits = 0;
    bool m_layoutDirty = false;
};

struct Application {
    Application()
    {
        if (QCoreApplication::instance() == nullptr) {
            app = std::make_unique<QCoreApplication>(argc, argv.data());
        }
    }

    int argc = 1;
    std::vector<char*> argv{const_cast<char*>("gs_app_tests"), nullptr};
    std::unique_ptr<QCoreApplication> app;
};

class FakeBench final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString folder READ folder CONSTANT FINAL)
    Q_PROPERTY(QVariantList files READ files CONSTANT FINAL)

public:
    explicit FakeBench(QString folder) : m_folder(std::move(folder)) {}

    [[nodiscard]] QString folder() const { return m_folder; }
    [[nodiscard]] QVariantList files() const { return {}; }

signals:
    void filesChanged();

private:
    QString m_folder;
};

void writeFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(file.write(contents) == contents.size());
}

const QString kSource = QStringLiteral("keys.F.onPress(async () => {\n"
                                       "  await touch.tap(0.5, 0.5)\n"
                                       "  await sleep(500)\n"
                                       "})\n");

QVariantMap nodeTitled(const BlueprintModel& model, const QString& title)
{
    for (const QVariant& node : model.graph().value(QStringLiteral("nodes")).toList()) {
        if (node.toMap().value(QStringLiteral("title")).toString() == title) {
            return node.toMap();
        }
    }
    return {};
}

QString keyTitled(const BlueprintModel& model, const QString& title)
{
    return nodeTitled(model, title).value(QStringLiteral("key")).toString();
}

bool is(const QVariantMap& map, const char* field, QMetaType::Type type)
{
    const auto found = map.constFind(QString::fromLatin1(field));
    return found != map.constEnd() && found->typeId() == type;
}

}

TEST_CASE("BP-APP-001 the model publishes the contract's shapes")
{
    const Application application;
    FakeText text(kSource);
    BlueprintModel model;
    model.setDocument(&text);
    REQUIRE(model.ready());
    CHECK(model.problem().isEmpty());
    const QVariantMap graph = model.graph();
    CHECK(is(graph, "key", QMetaType::QString));
    CHECK(is(graph, "kind", QMetaType::QString));
    CHECK(is(graph, "title", QMetaType::QString));
    CHECK(is(graph, "readOnly", QMetaType::Bool));
    CHECK(is(graph, "nodes", QMetaType::QVariantList));
    CHECK(is(graph, "wires", QMetaType::QVariantList));
    CHECK(is(graph, "comments", QMetaType::QVariantList));
    CHECK(is(graph, "problem", QMetaType::QString));
    CHECK(is(graph, "problemLine", QMetaType::Int));
    const QVariantList nodes = graph.value(QStringLiteral("nodes")).toList();
    REQUIRE(nodes.size() == 3);
    for (const QVariant& value : nodes) {
        const QVariantMap node = value.toMap();
        for (const char* field : {"key", "kind", "tone", "icon", "title", "badge", "subtitle", "note", "code", "breakpoint",
                                  "breakpointReason", "problem"}) {
            CHECK_MESSAGE(is(node, field, QMetaType::QString), field);
        }
        for (const char* field : {"x", "y", "activeSince"}) {
            CHECK_MESSAGE(is(node, field, QMetaType::Double), field);
        }
        for (const char* field : {"floating", "start", "active", "visited"}) {
            CHECK_MESSAGE(is(node, field, QMetaType::Bool), field);
        }
        CHECK(is(node, "inputs", QMetaType::QVariantList));
        CHECK(is(node, "outputs", QMetaType::QVariantList));
        for (const char* side : {"inputs", "outputs"}) {
            for (const QVariant& pinValue : node.value(QString::fromLatin1(side)).toList()) {
                const QVariantMap pin = pinValue.toMap();
                for (const char* field : {"name", "label", "type", "value", "editor", "preview"}) {
                    CHECK_MESSAGE(is(pin, field, QMetaType::QString), field);
                }
                CHECK(is(pin, "connected", QMetaType::Bool));
                CHECK(is(pin, "optional", QMetaType::Bool));
                CHECK(is(pin, "choices", QMetaType::QVariantList));
                CHECK(pin.contains(QStringLiteral("raw")));
            }
        }
    }
    const QVariantList wires = graph.value(QStringLiteral("wires")).toList();
    REQUIRE(wires.size() == 2);
    for (const QVariant& value : wires) {
        const QVariantMap wire = value.toMap();
        for (const char* field : {"key", "from", "fromPin", "to", "toPin", "type", "label"}) {
            CHECK_MESSAGE(is(wire, field, QMetaType::QString), field);
        }
        CHECK(is(wire, "active", QMetaType::Bool));
        CHECK(is(wire, "dashed", QMetaType::Bool));
    }
    CHECK(nodeTitled(model, QStringLiteral("Key F")).value(QStringLiteral("badge")).toString() == "Pressed");
    const QVariantMap tap = nodeTitled(model, QStringLiteral("Tap Screen"));
    REQUIRE_FALSE(tap.isEmpty());
    const QVariantMap point = tap.value(QStringLiteral("inputs")).toList().at(1).toMap();
    CHECK(point.value(QStringLiteral("type")).toString() == "point");
    CHECK(point.value(QStringLiteral("value")).toString() == "50%, 50%");
    CHECK(point.value(QStringLiteral("raw")).toMap().value(QStringLiteral("x")).toDouble() == doctest::Approx(0.5));
    CHECK(model.pages().size() == 1);
    CHECK(model.pages().front().toMap().value(QStringLiteral("title")).toString() == "Main");
    CHECK(model.crumbs().size() == 1);
    CHECK(model.viewState().value(QStringLiteral("zoom")).toDouble() == doctest::Approx(1.0));
}

TEST_CASE("BP-APP-002 every edit is one undo step, text and layout interleaved")
{
    const Application application;
    FakeText text(kSource);
    BlueprintModel model;
    model.setDocument(&text);
    REQUIRE(model.ready());
    CHECK_FALSE(model.canUndo());
    const QString wait = keyTitled(model, QStringLiteral("Delay"));
    REQUIRE_FALSE(wait.isEmpty());
    CHECK(model.setValue(wait, QStringLiteral("ms"), 800).isEmpty());
    CHECK(text.edits() == 1);
    CHECK(text.text().contains(QStringLiteral("sleep(800)")));
    CHECK(model.canUndo());
    const QString tap = keyTitled(model, QStringLiteral("Tap Screen"));
    const double before = nodeTitled(model, QStringLiteral("Tap Screen")).value(QStringLiteral("x")).toDouble();
    CHECK(model.move({tap}, 100, 20).isEmpty());
    CHECK(model.layoutDirty());
    CHECK(text.layoutDirty());
    CHECK(nodeTitled(model, QStringLiteral("Tap Screen")).value(QStringLiteral("x")).toDouble()
          == doctest::Approx(before + 100));
    CHECK(text.edits() == 1);
    CHECK(model.setValue(keyTitled(model, QStringLiteral("Delay")), QStringLiteral("ms"), 900).isEmpty());
    CHECK(text.text().contains(QStringLiteral("sleep(900)")));

    CHECK(model.undo());
    CHECK(text.text().contains(QStringLiteral("sleep(800)")));
    CHECK(nodeTitled(model, QStringLiteral("Tap Screen")).value(QStringLiteral("x")).toDouble()
          == doctest::Approx(before + 100));
    CHECK(model.undo());
    CHECK(text.text().contains(QStringLiteral("sleep(800)")));
    CHECK(nodeTitled(model, QStringLiteral("Tap Screen")).value(QStringLiteral("x")).toDouble() == doctest::Approx(before));
    CHECK(model.undo());
    CHECK(text.text() == kSource);
    CHECK_FALSE(model.canUndo());
    CHECK(model.canRedo());
    CHECK_FALSE(model.layoutDirty());
    CHECK_FALSE(text.layoutDirty());

    CHECK(model.redo());
    CHECK(text.text().contains(QStringLiteral("sleep(800)")));
    CHECK_FALSE(model.layoutDirty());
    CHECK(model.redo());
    CHECK(nodeTitled(model, QStringLiteral("Tap Screen")).value(QStringLiteral("x")).toDouble()
          == doctest::Approx(before + 100));
    CHECK(model.layoutDirty());
    CHECK(model.redo());
    CHECK(text.text().contains(QStringLiteral("sleep(900)")));
    CHECK_FALSE(model.canRedo());

    text.type(kSource);
    QCoreApplication::processEvents();
    CHECK(nodeTitled(model, QStringLiteral("Delay")).value(QStringLiteral("inputs")).toList().at(1).toMap().value(
              QStringLiteral("raw"))
          == 500);
}

TEST_CASE("BP-APP-003 details of an event node and probe positions")
{
    const Application application;
    FakeText text(kSource);
    BlueprintModel model;
    model.setDocument(&text);
    REQUIRE(model.ready());
    const QString event = keyTitled(model, QStringLiteral("Key F"));
    model.setSelection({event});
    const QVariantMap details = model.details();
    CHECK(details.value(QStringLiteral("title")).toString() == "Key event");
    QStringList fields;
    for (const QVariant& group : details.value(QStringLiteral("groups")).toList()) {
        for (const QVariant& field : group.toMap().value(QStringLiteral("fields")).toList()) {
            fields.push_back(field.toMap().value(QStringLiteral("name")).toString());
        }
    }
    CHECK(fields.contains(QStringLiteral("key")));
    CHECK(fields.contains(QStringLiteral("when")));
    CHECK(fields.contains(QStringLiteral("note")));
    CHECK(model.editDetail(QStringLiteral("key"), QStringLiteral("G")).isEmpty());
    CHECK(text.text().startsWith(QStringLiteral("keys.G.onPress(")));

    const QString tap = keyTitled(model, QStringLiteral("Tap Screen"));
    CHECK(model.nodeAt(QStringLiteral("2:3")) == tap);
    std::vector<std::pair<QString, QString>> pulses;
    QObject::connect(&model, &BlueprintModel::pulse,
                     [&pulses](const QString& node, const QString& wire) { pulses.emplace_back(node, wire); });
    model.setDebugging(true);
    model.hit(QStringLiteral("2:3"));
    model.hit(QStringLiteral("3:3"));
    REQUIRE(pulses.size() == 2);
    CHECK(pulses.at(0).first == tap);
    CHECK(pulses.at(1).first == keyTitled(model, QStringLiteral("Delay")));
    CHECK_FALSE(pulses.at(1).second.isEmpty());
    CHECK(model.reached().size() == 2);
}

TEST_CASE("BP-APP-004 the machine feed marks the active state, the visited one and the transition")
{
    const Application application;
    FakeText text(QStringLiteral("await machine({\n  async a(to) {\n    return to.b(\"go\")\n  },\n  async b(to) {\n  },\n})\n"));
    BlueprintModel model;
    model.setDocument(&text);
    REQUIRE(model.ready());
    model.setPage(QStringLiteral("machine:1:7"));
    REQUIRE(model.page() == QStringLiteral("machine:1:7"));
    CHECK(model.graph().value(QStringLiteral("kind")).toString() == "machine");
    model.setDebugging(true);
    model.machineState(QStringLiteral("1:7"), QStringLiteral("a"), QString(), 1000);
    model.machineState(QStringLiteral("1:7"), QStringLiteral("b"), QStringLiteral("go"), 2000);
    const QVariantMap a = nodeTitled(model, QStringLiteral("a"));
    const QVariantMap b = nodeTitled(model, QStringLiteral("b"));
    CHECK(b.value(QStringLiteral("active")).toBool());
    CHECK(b.value(QStringLiteral("activeSince")).toDouble() == doctest::Approx(2000));
    CHECK_FALSE(a.value(QStringLiteral("active")).toBool());
    CHECK(a.value(QStringLiteral("visited")).toBool());
    const QVariantList wires = model.graph().value(QStringLiteral("wires")).toList();
    REQUIRE(wires.size() == 1);
    CHECK(wires.front().toMap().value(QStringLiteral("active")).toBool());
    CHECK(wires.front().toMap().value(QStringLiteral("label")).toString() == "go");
    model.setSelection({a.value(QStringLiteral("key")).toString()});
    bool logged = false;
    for (const QVariant& group : model.details().value(QStringLiteral("groups")).toList()) {
        for (const QVariant& field : group.toMap().value(QStringLiteral("fields")).toList()) {
            if (field.toMap().value(QStringLiteral("name")).toString() == "log") {
                logged = field.toMap().value(QStringLiteral("entries")).toList().size() == 1;
            }
        }
    }
    CHECK(logged);
    const QVariantMap instrumented = model.instrument(text.text());
    CHECK(instrumented.value(QStringLiteral("problems")).toStringList().isEmpty());
    CHECK(instrumented.value(QStringLiteral("javascript")).toString().contains(QStringLiteral("__gs.enter(\"2:10\")")));

    model.machineState(QStringLiteral("7"), QStringLiteral("a"), QStringLiteral("again"), 3000);
    CHECK(nodeTitled(model, QStringLiteral("a")).value(QStringLiteral("active")).toBool());
    model.machineState(QStringLiteral("7"), QString(), QString(), 4000);
    CHECK_FALSE(nodeTitled(model, QStringLiteral("a")).value(QStringLiteral("active")).toBool());
    CHECK_FALSE(nodeTitled(model, QStringLiteral("b")).value(QStringLiteral("active")).toBool());
    const std::optional<std::string> run = gs::app::instrumentModule({}, "logic.ts", text.text().toStdString());
    REQUIRE(run.has_value());
    CHECK(run->starts_with("const __gs=__gsFor(\"logic.ts\");"));
}

TEST_CASE("BP-APP-004 states played back from a recording have no running clock and log the time in the recording")
{
    const Application application;
    FakeText text(QStringLiteral("await machine({\n  async a(to) {\n    return to.b(\"go\")\n  },\n  async b(to) {\n  },\n})\n"));
    BlueprintModel model;
    model.setDocument(&text);
    REQUIRE(model.ready());
    model.setPage(QStringLiteral("machine:1:7"));
    model.machineState(QStringLiteral("7"), QStringLiteral("a"), QString(), 0, 0);
    model.machineState(QStringLiteral("7"), QStringLiteral("b"), QStringLiteral("go"), 0, 1500);
    const QVariantMap b = nodeTitled(model, QStringLiteral("b"));
    CHECK(b.value(QStringLiteral("active")).toBool());
    CHECK(b.value(QStringLiteral("activeSince")).toDouble() == doctest::Approx(0));
    CHECK(nodeTitled(model, QStringLiteral("a")).value(QStringLiteral("visited")).toBool());
    model.setSelection({nodeTitled(model, QStringLiteral("a")).value(QStringLiteral("key")).toString()});
    QVariantList entries;
    for (const QVariant& group : model.details().value(QStringLiteral("groups")).toList()) {
        for (const QVariant& field : group.toMap().value(QStringLiteral("fields")).toList()) {
            if (field.toMap().value(QStringLiteral("name")).toString() == "log") {
                entries = field.toMap().value(QStringLiteral("entries")).toList();
            }
        }
    }
    REQUIRE(entries.size() == 1);
    CHECK(entries.front().toMap().value(QStringLiteral("time")).toString() == QStringLiteral("00:01.5"));
    CHECK(entries.front().toMap().value(QStringLiteral("text")).toString() == QStringLiteral("go → b"));

    model.resetMachines();
    CHECK_FALSE(nodeTitled(model, QStringLiteral("b")).value(QStringLiteral("active")).toBool());
    CHECK_FALSE(nodeTitled(model, QStringLiteral("a")).value(QStringLiteral("visited")).toBool());
}

TEST_CASE("ADR-0091 00:5x a component's own file opens as its page, and the blueprint reopens on its last view")
{
    const Application application;
    QTemporaryDir folder;
    REQUIRE(folder.isValid());
    const QByteArray profile = QByteArray(R"json({"format":"gamescrcpy.profile","version":3,"controls":[
        {"id":"fire","name":"Fire","type":"hold","inputs":{"press":"Space"}},
        {"id":"jump","type":"tap","inputs":{"press":"Mouse.Right"}},
        {"id":"spare","type":"tap"}]})json");
    writeFile(QDir(folder.path()).filePath(QStringLiteral("profile.json")), profile);
    const QString logic = QStringLiteral("controls.fire.onPress(async () => {\n"
                                         "  await touch.tap(0.5, 0.5)\n"
                                         "})\n"
                                         "keys.F.onPress(() => {})\n");
    writeFile(QDir(folder.path()).filePath(QStringLiteral("logic.ts")), logic.toUtf8());
    const QString own = QStringLiteral("controls.fire.onRelease(() => {})\n");
    REQUIRE(QDir(folder.path()).mkpath(QStringLiteral("controls")));
    writeFile(QDir(folder.path()).filePath(QStringLiteral("controls/fire.ts")), own.toUtf8());
    const auto hasNode = [](const BlueprintModel& model, const QString& prefix) {
        for (const QVariant& node : model.graph().value(QStringLiteral("nodes")).toList()) {
            if (node.toMap().value(QStringLiteral("key")).toString().startsWith(prefix)) {
                return true;
            }
        }
        return false;
    };

    FakeBench bench(folder.path());
    FakeText main(logic);
    BlueprintModel model;
    model.setBench(&bench);
    model.setDocument(&main);
    REQUIRE(model.ready());
    CHECK(model.pages().size() == 1);
    CHECK(hasNode(model, QStringLiteral("e:controls.fire.onPress")));

    FakeText file(own, QStringLiteral("controls/fire.ts"));
    model.setDocument(&file);
    REQUIRE(model.ready());
    const QVariantList pages = model.pages();
    REQUIRE(pages.size() == 1);
    CHECK(pages.front().toMap().value(QStringLiteral("title")).toString() == QStringLiteral("Fire · Space"));
    CHECK(model.graph().value(QStringLiteral("title")).toString() == QStringLiteral("Fire · Space"));
    CHECK(hasNode(model, QStringLiteral("e:controls.fire.onRelease")));
    model.setView(120, 80, 1.25);
    REQUIRE(QFile::exists(QDir(folder.path()).filePath(QStringLiteral("controls/fire.blueprint.json"))));

    FakeText again(own, QStringLiteral("controls/fire.ts"));
    BlueprintModel second;
    second.setBench(&bench);
    second.setDocument(&again);
    REQUIRE(second.ready());
    CHECK(second.viewState().value(QStringLiteral("x")).toDouble() == doctest::Approx(120));
    CHECK(second.viewState().value(QStringLiteral("y")).toDouble() == doctest::Approx(80));
    CHECK(second.viewState().value(QStringLiteral("zoom")).toDouble() == doctest::Approx(1.25));
}

TEST_CASE("ADR-0091 the exported functions of library.ts are blocks in the logic, and the library's own page is titled")
{
    const Application application;
    QTemporaryDir folder;
    REQUIRE(folder.isValid());
    const QString library = QStringLiteral("export async function burst(count: number) {\n}\n");
    writeFile(QDir(folder.path()).filePath(QStringLiteral("library.ts")), library.toUtf8());
    FakeBench bench(folder.path());
    FakeText main(QStringLiteral("import { burst } from \"./library.ts\"\n"
                                 "keys.F.onPress(async () => {\n"
                                 "  await burst(3)\n"
                                 "})\n"));
    BlueprintModel model;
    model.setBench(&bench);
    model.setDocument(&main);
    REQUIRE(model.ready());
    bool listed = false;
    for (const QVariant& entry : model.catalog({}, {}, {})) {
        listed = listed || entry.toMap().value(QStringLiteral("id")).toString() == QStringLiteral("call:lib:burst");
    }
    CHECK(listed);
    const QVariantMap call = nodeTitled(model, QStringLiteral("burst"));
    REQUIRE_FALSE(call.isEmpty());
    CHECK(call.value(QStringLiteral("badge")).toString() == QStringLiteral("Library"));
    CHECK(model.open(call.value(QStringLiteral("key")).toString()) == QStringLiteral("file:library.ts"));
    CHECK(model.page().isEmpty());

    FakeText own(library, QStringLiteral("library.ts"));
    model.setDocument(&own);
    REQUIRE(model.ready());
    CHECK(model.pages().front().toMap().value(QStringLiteral("title")).toString() == QStringLiteral("Function Library"));
    bool self = false;
    for (const QVariant& entry : model.catalog({}, {}, {})) {
        self = self || entry.toMap().value(QStringLiteral("id")).toString().startsWith(u"call:lib:");
    }
    CHECK_FALSE(self);
}

TEST_CASE("BP-APP-005 the block picker lists the blocks to add by name, the text reading blocks among them")
{
    const Application application;
    FakeText text(kSource);
    BlueprintModel model;
    model.setDocument(&text);
    REQUIRE(model.ready());
    CHECK(model.catalog({}, {}, {}).size() > 50);
    const auto titles = [&model](const QString& query) {
        QStringList found;
        for (const QVariant& entry : model.catalog({}, {}, query)) {
            found.push_back(entry.toMap().value(QStringLiteral("id")).toString() + " "
                            + entry.toMap().value(QStringLiteral("title")).toString());
        }
        return found;
    };
    CHECK(titles(QStringLiteral("recognize text")).contains(QStringLiteral("call:vision.readText Recognize Text")));
    CHECK(titles(QStringLiteral("find text")).contains(QStringLiteral("call:vision.findText Find Text")));
    CHECK(titles(QStringLiteral("capture")).contains(QStringLiteral("call:vision.capture Capture Region")));
    const auto descriptionOf = [&model](const QString& id) {
        for (const QVariant& entry : model.catalog({}, {}, {})) {
            if (entry.toMap().value(QStringLiteral("id")).toString() == id) {
                return entry.toMap().value(QStringLiteral("description")).toString();
            }
        }
        return QStringLiteral("(not listed)");
    };
    CHECK(descriptionOf(QStringLiteral("call:vision.readText")).startsWith(u"Reads"));
    CHECK(descriptionOf(QStringLiteral("flow:sequence")).isEmpty());
}

TEST_CASE("BP-APP-007 undoing every step back to the saved code and layout clears the unsaved mark")
{
    const Application application;
    FakeText text(kSource);
    BlueprintModel model;
    model.setDocument(&text);
    REQUIRE(model.ready());
    CHECK_FALSE(text.layoutDirty());
    const QString floating =
        model.add(QStringLiteral("call:sleep"), 400, 300, {}, {}).value(QStringLiteral("key")).toString();
    REQUIRE_FALSE(floating.isEmpty());
    CHECK(text.text() == kSource);
    CHECK(text.layoutDirty());
    QString wire;
    for (const QVariant& value : model.graph().value(QStringLiteral("wires")).toList()) {
        const QVariantMap item = value.toMap();
        if (item.value(QStringLiteral("type")).toString() == u"exec") {
            wire = item.value(QStringLiteral("key")).toString();
            break;
        }
    }
    REQUIRE_FALSE(wire.isEmpty());
    CHECK(model.insertOnWire(wire, floating).value(QStringLiteral("problem")).toString().isEmpty());
    CHECK(text.text() != kSource);
    REQUIRE(model.undo());
    CHECK(text.text() == kSource);
    CHECK(text.layoutDirty());
    REQUIRE(model.undo());
    CHECK(text.text() == kSource);
    CHECK_FALSE(text.layoutDirty());
}

TEST_CASE("BP-APP-006 the marks of a draft compare its blocks with the blocks of the version in use")
{
    const Application application;
    FakeText text(kSource);
    BlueprintModel model;
    model.setDocument(&text);
    REQUIRE(model.ready());
    model.setDiffBase(kSource);
    for (const QVariant& value : model.graph().value(QStringLiteral("nodes")).toList()) {
        CHECK(value.toMap().value(QStringLiteral("diff")).toString().isEmpty());
    }
    CHECK(model.diffRemoved().isEmpty());

    text.type(QStringLiteral("keys.F.onPress(async () => {\n"
                             "  await touch.tap(0.2, 0.8)\n"
                             "  log.info(\"ready\")\n"
                             "})\n"));
    QCoreApplication::processEvents();
    int added = 0;
    int changed = 0;
    for (const QVariant& value : model.graph().value(QStringLiteral("nodes")).toList()) {
        const QString mark = value.toMap().value(QStringLiteral("diff")).toString();
        added += mark == u"added" ? 1 : 0;
        changed += mark == u"changed" ? 1 : 0;
    }
    CHECK(added >= 1);
    CHECK(changed >= 1);
    bool removedSleep = false;
    for (const QVariant& value : model.diffRemoved()) {
        removedSleep = removedSleep || value.toMap().value(QStringLiteral("key")).toString().contains(u"sleep");
    }
    CHECK(removedSleep);

    model.setDiffBase(QString());
    for (const QVariant& value : model.graph().value(QStringLiteral("nodes")).toList()) {
        CHECK(value.toMap().value(QStringLiteral("diff")).toString().isEmpty());
    }
    CHECK(model.diffRemoved().isEmpty());
}

TEST_CASE("BP-APP-008 the graph and the sidebar reach QML as JSON text with the same content as the model")
{
    const Application application;
    QString source = QStringLiteral("keys.F.onPress(async () => {\n");
    for (int index = 1; index <= 60; ++index) {
        source += QStringLiteral("  await sleep(%1)\n").arg(index);
    }
    source += QStringLiteral("})\n");
    FakeText text(source);
    BlueprintModel model;
    model.setDocument(&text);
    REQUIRE(model.ready());
    QJsonParseError problem{};
    const QJsonDocument graph = QJsonDocument::fromJson(model.graphText().toUtf8(), &problem);
    REQUIRE(problem.error == QJsonParseError::NoError);
    CHECK(graph == QJsonDocument::fromVariant(model.graph()));
    CHECK(graph.object().value(QStringLiteral("nodes")).toArray().size() == 61);
    const QJsonDocument sidebar = QJsonDocument::fromJson(model.sidebarText().toUtf8(), &problem);
    REQUIRE(problem.error == QJsonParseError::NoError);
    CHECK(sidebar == QJsonDocument::fromVariant(model.sidebar()));
    CHECK_FALSE(sidebar.array().isEmpty());

    model.setDocument(nullptr);
    CHECK(model.graphText() == QStringLiteral("{}"));
    CHECK(model.sidebarText() == QStringLiteral("[]"));
}

#include "BlueprintModelTests.moc"
