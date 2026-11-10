#include "ComponentLibrary.h"
#include "Declarations.h"

#include <gs/script/ApiFiles.h>

#include <doctest/doctest.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using gs::app::chordForKey;
using gs::app::ComponentEntry;
using gs::app::ComponentLibrary;
using gs::app::componentPropsIn;
using gs::app::DeclaredSpec;

namespace {

std::filesystem::path pathOf(const QTemporaryDir& dir)
{
    return {dir.path().toStdWString()};
}

std::filesystem::path pathOf(const QString& text)
{
    return {text.toStdWString()};
}

std::filesystem::path examplesFolder()
{
    return pathOf(QString::fromUtf8(GS_EXAMPLE_COMPONENTS_DIR));
}

std::filesystem::path startersFolder()
{
    return pathOf(QString::fromUtf8(GS_COMPONENT_TEST_DATA_DIR));
}

void writeText(const std::filesystem::path& file, const std::string& text)
{
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << text;
}

std::string readLines(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string lines;
    for (const char c : text) {
        if (c != '\r') {
            lines += c;
        }
    }
    return lines;
}

}

TEST_CASE("ADR-0091: a component's properties are read from static props without running the code")
{
    const std::vector<DeclaredSpec> props = componentPropsIn(QStringLiteral(R"(
export default class Combo extends Component {
    static props = {
        key: prop.key("q", { label: "Key" }),
        spare: prop.key(),
        target: prop.position(undefined, { label: "Target" }),
        times: prop.number(3, { min: 1, max: 9 }),
        word: prop.text("gg"),
        loop: prop.toggle(true),
        partner: prop.component("fire"),
        later: prop.later(1),
    };

    setup() {
        const local = { other: prop.number(5) };
    }
}
)"));
    REQUIRE(props.size() == 7);
    CHECK(props[0].name == QStringLiteral("key"));
    CHECK(props[0].kind == QStringLiteral("key"));
    CHECK(props[0].value.toString() == QStringLiteral("KeyQ"));
    CHECK(props[0].label == QStringLiteral("Key"));
    CHECK(props[1].kind == QStringLiteral("key"));
    CHECK_FALSE(props[1].value.isValid());
    CHECK(props[2].kind == QStringLiteral("position"));
    CHECK(props[2].label == QStringLiteral("Target"));
    CHECK(props[3].value.toDouble() == 3.0);
    REQUIRE(props[3].max.has_value());
    CHECK(*props[3].max == 9.0);
    CHECK(props[4].value.toString() == QStringLiteral("gg"));
    CHECK(props[5].value == QVariant(true));
    CHECK(props[6].kind == QStringLiteral("component"));
    CHECK(props[6].value.toString() == QStringLiteral("fire"));
    CHECK(componentPropsIn(QStringLiteral("export default class A extends Component {}")).empty());
}

TEST_CASE("ADR-0091: key names in static props are written as in profiles, the way the prelude reads them")
{
    CHECK(chordForKey(QStringLiteral("F")) == QStringLiteral("KeyF"));
    CHECK(chordForKey(QStringLiteral("f")) == QStringLiteral("KeyF"));
    CHECK(chordForKey(QStringLiteral("5")) == QStringLiteral("Digit5"));
    CHECK(chordForKey(QStringLiteral("Esc")) == QStringLiteral("Escape"));
    CHECK(chordForKey(QStringLiteral("Shift")) == QStringLiteral("ShiftLeft"));
    CHECK(chordForKey(QStringLiteral("WheelUp")) == QStringLiteral("Mouse.WheelUp"));
    CHECK(chordForKey(QStringLiteral("MouseLeft")) == QStringLiteral("Mouse.Left"));
    CHECK(chordForKey(QStringLiteral("GamepadA")) == QStringLiteral("Gamepad.A"));
    CHECK(chordForKey(QStringLiteral("KeyW")) == QStringLiteral("KeyW"));
    CHECK(chordForKey(QStringLiteral("Mouse.Right")) == QStringLiteral("Mouse.Right"));
}

TEST_CASE("ADR-0091: the example components are listed read-only with the properties they declare")
{
    QTemporaryDir mine;
    REQUIRE(mine.isValid());
    ComponentLibrary library(examplesFolder(), pathOf(mine));
    CHECK(library.examples().size() == 5);
    CHECK(library.mine().isEmpty());
    for (const char* id : {"toggle-hold", "double-tap", "burst", "in-turn"}) {
        INFO(id);
        const ComponentEntry* entry = library.find(QStringLiteral("examples/") + QString::fromLatin1(id));
        REQUIRE(entry != nullptr);
        CHECK(entry->example);
        CHECK_FALSE(entry->name.isEmpty());
        CHECK_FALSE(entry->description.isEmpty());
        REQUIRE_FALSE(entry->props.empty());
        CHECK(entry->props[0].kind == QStringLiteral("key"));
        CHECK(entry->props[0].value.toString() == QStringLiteral("KeyF"));
        CHECK(readLines(pathOf(entry->folder) / "tsconfig.json") == gs::script::profileTsconfig());
    }
    const ComponentEntry* burst = library.find(QStringLiteral("examples/burst"));
    CHECK(burst->name == QStringLiteral("Burst"));
    CHECK(burst->icon == QStringLiteral("zap"));
    REQUIRE(burst->props.size() == 3);
    CHECK(burst->props[1].name == QStringLiteral("times"));
    CHECK(burst->props[1].value.toDouble() == 3.0);
    const ComponentEntry* turn = library.find(QStringLiteral("examples/in-turn"));
    REQUIRE(turn->props.size() == 3);
    CHECK(turn->props[1].kind == QStringLiteral("position"));
    CHECK(turn->props[2].label == QStringLiteral("Third place"));
    CHECK_FALSE(library.setInfo(QStringLiteral("examples/burst"), QStringLiteral("name"), QStringLiteral("Mine")));
    CHECK_FALSE(library.remove(QStringLiteral("examples/burst")));
    CHECK(library.copyToMine(QStringLiteral("mine/none")).isEmpty());
    CHECK(library.info(QStringLiteral("mine/none")).isEmpty());
}

TEST_CASE("INP-012: the example that shows all key hints while held is listed read-only with one key that starts unbound")
{
    QTemporaryDir mine;
    REQUIRE(mine.isValid());
    ComponentLibrary library(examplesFolder(), pathOf(mine));
    const ComponentEntry* entry = library.find(QStringLiteral("examples/show-all-hints"));
    REQUIRE(entry != nullptr);
    CHECK(entry->example);
    CHECK(entry->name == QStringLiteral("Show all key hints while held"));
    CHECK(entry->description == QStringLiteral("Hold to see every key at full opacity"));
    CHECK(entry->icon == QStringLiteral("eye"));
    CHECK(entry->className == QStringLiteral("ShowAllHints"));
    CHECK(entry->type.isEmpty());
    CHECK(entry->parent.isEmpty());
    CHECK(entry->children.empty());
    REQUIRE(entry->props.size() == 1);
    CHECK(entry->props[0].name == QStringLiteral("key"));
    CHECK(entry->props[0].kind == QStringLiteral("key"));
    CHECK(entry->props[0].label == QStringLiteral("Key"));
    CHECK_FALSE(entry->props[0].value.isValid());
    CHECK(readLines(pathOf(entry->folder) / "tsconfig.json") == gs::script::profileTsconfig());
    const QVariantMap shown = library.info(QStringLiteral("examples/show-all-hints"));
    CHECK(shown.value(QStringLiteral("example")).toBool());
    CHECK(shown.value(QStringLiteral("props")).toList().size() == 1);
    CHECK_FALSE(
        library.setInfo(QStringLiteral("examples/show-all-hints"), QStringLiteral("icon"), QStringLiteral("zap")));
}

TEST_CASE("ADR-0091: my components can be made, copied from an example and described, and are there after a restart")
{
    QTemporaryDir mine;
    REQUIRE(mine.isValid());
    QString first;
    {
        ComponentLibrary library(examplesFolder(), pathOf(mine));
        first = library.add(QStringLiteral("Combo!"));
        CHECK(first == QStringLiteral("mine/combo"));
        CHECK(library.add(QStringLiteral("Combo!")) == QStringLiteral("mine/combo-2"));
        REQUIRE(library.find(QStringLiteral("mine/combo-2")) != nullptr);
        CHECK(library.find(QStringLiteral("mine/combo-2"))->name == QStringLiteral("Combo! 2"));
        const ComponentEntry* made = library.find(first);
        REQUIRE(made != nullptr);
        CHECK_FALSE(made->example);
        CHECK(made->icon == QStringLiteral("sparkles"));
        REQUIRE(made->props.size() == 1);
        CHECK(made->props[0].name == QStringLiteral("key"));
        CHECK(made->props[0].value.toString() == QStringLiteral("KeyF"));
        const QDir folder(made->folder);
        CHECK(QFile::exists(folder.filePath(QStringLiteral("component.ts"))));
        CHECK(QFile::exists(folder.filePath(QStringLiteral("tsconfig.json"))));
        QFile info(folder.filePath(QStringLiteral("component.json")));
        REQUIRE(info.open(QIODevice::ReadOnly));
        const QJsonObject json = QJsonDocument::fromJson(info.readAll()).object();
        info.close();
        CHECK(json.value(QStringLiteral("format")).toString() == QStringLiteral("gamescrcpy.component"));
        CHECK(json.value(QStringLiteral("version")).toInt() == 1);
        CHECK(json.value(QStringLiteral("name")).toString() == QStringLiteral("Combo!"));

        const QString copy = library.copyToMine(QStringLiteral("examples/double-tap"));
        CHECK(copy == QStringLiteral("mine/double-tap-2"));
        REQUIRE(library.find(copy) != nullptr);
        CHECK(library.find(copy)->name == QStringLiteral("Double tap 2"));
        CHECK(library.find(copy)->props.size() == 2);
        CHECK(QFile::exists(QDir(library.find(copy)->folder).filePath(QStringLiteral("tsconfig.json"))));
        CHECK(library.copyToMine(QStringLiteral("examples/double-tap")) == QStringLiteral("mine/double-tap-3"));
        CHECK(library.copyToMine(copy).isEmpty());
        CHECK(library.add(QStringLiteral("Burst")) == QStringLiteral("mine/burst-2"));
        CHECK(library.find(QStringLiteral("mine/burst-2"))->name == QStringLiteral("Burst 2"));

        CHECK(library.setInfo(first, QStringLiteral("icon"), QStringLiteral("zap")));
        CHECK_FALSE(library.setInfo(first, QStringLiteral("icon"), QStringLiteral("no-such-icon")));
        CHECK_FALSE(library.setInfo(first, QStringLiteral("name"), QStringLiteral("  ")));
        CHECK_FALSE(library.setInfo(first, QStringLiteral("colour"), QStringLiteral("red")));
        CHECK(library.setInfo(first, QStringLiteral("description"), QStringLiteral("Two keys in a row")));
        CHECK(library.setInfo(first, QStringLiteral("category"), QStringLiteral("Fight")));
        CHECK(library.mine().size() == 5);
    }
    ComponentLibrary reread(examplesFolder(), pathOf(mine));
    const ComponentEntry* again = reread.find(first);
    REQUIRE(again != nullptr);
    CHECK(again->name == QStringLiteral("Combo!"));
    CHECK(again->icon == QStringLiteral("zap"));
    CHECK(again->description == QStringLiteral("Two keys in a row"));
    CHECK(again->category == QStringLiteral("Fight"));
    CHECK_FALSE(reread.info(first).value(QStringLiteral("example")).toBool());
    CHECK(reread.mine().size() == 5);
}

TEST_CASE("ADR-0091: a bundled component is reused when its code is the same and kept as \"name 2\" when it differs")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const std::filesystem::path base = pathOf(root);
    ComponentLibrary library(base / "examples", base / "mine");
    const QString mine = library.add(QStringLiteral("Combo"));
    REQUIRE(library.find(mine) != nullptr);
    const std::filesystem::path made = pathOf(library.find(mine)->folder);

    const std::filesystem::path same = base / "bundle-same";
    std::filesystem::copy(made, same, std::filesystem::copy_options::recursive);
    CHECK(library.install(same) == mine);
    CHECK(library.mine().size() == 1);

    const std::filesystem::path other = base / "bundle-other";
    std::filesystem::copy(made, other, std::filesystem::copy_options::recursive);
    writeText(other / "component.ts", "export default class Other extends Component {}\n");
    const QString installed = library.install(other);
    CHECK(installed == QStringLiteral("mine/combo-2"));
    REQUIRE(library.find(installed) != nullptr);
    CHECK(library.find(installed)->name == QStringLiteral("Combo 2"));
    CHECK(library.find(mine)->name == QStringLiteral("Combo"));
    CHECK(library.install(base / "bundle-missing").isEmpty());

    QStringList saved;
    QObject::connect(&library, &ComponentLibrary::componentSaved, [&saved](const QString& ref) { saved.append(ref); });
    writeText(made / "component.ts", "export default class Changed extends Component {}\n");
    library.reload();
    CHECK(saved == QStringList{mine});
    CHECK(library.setInfo(mine, QStringLiteral("icon"), QStringLiteral("zap")));
    CHECK(saved == QStringList{mine});
}

TEST_CASE("ADR-0091: a component's base, inherited properties and children are read without running the code")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const std::filesystem::path base = pathOf(root);
    writeText(base / "mine" / "quick" / "component.ts", R"js(export default class Quick extends Hold {
    static props = { boost: prop.key("B"), press: prop.key("P"), times: prop.number(2) };
    cancel = this.child(Tap);

    onPressed() {}
}
)js");
    writeText(base / "mine" / "quicker" / "component.ts", R"js(import Quick from "../quick/component.ts";

export default class Quicker extends Quick {
    // it's quicker { than the base
    static props = {
        times: prop.number(5),
        word: prop.text("go"),
        target: prop.component(Skill),
        any: prop.component(),
        first: prop.component("jump"),
    };
    skills: Skill[] = [this.child(Skill), null, this.child(Skill)];
    label = "this.child(Hold)";
    static spare = 1;

    setup() {
        const local = this.child(Tap);
    }
}
)js");
    writeText(base / "mine" / "plain" / "component.ts", "export default class Plain extends Component {}\n");
    writeText(base / "mine" / "named" / "component.ts", "class Named extends Wasd {}\nexport default Named;\n");
    writeText(base / "mine" / "loop-a" / "component.ts",
              "import B from \"../loop-b/component.ts\";\nexport default class A extends B {}\n");
    writeText(base / "mine" / "loop-b" / "component.ts",
              "import A from \"../loop-a/component.ts\";\nexport default class B extends A {}\n");
    writeText(base / "mine" / "orphan" / "component.ts",
              "import Hold from \"../gone/component.ts\";\nexport default class Orphan extends Hold {}\n");
    ComponentLibrary library(base / "examples", base / "mine");

    const ComponentEntry* quick = library.find(QStringLiteral("mine/quick"));
    REQUIRE(quick != nullptr);
    CHECK(quick->type == QStringLiteral("hold"));
    CHECK(quick->parent.isEmpty());
    CHECK(quick->className == QStringLiteral("Quick"));
    REQUIRE(quick->props.size() == 2);
    CHECK(quick->props[0].name == QStringLiteral("boost"));
    CHECK(quick->props[1].name == QStringLiteral("times"));
    REQUIRE(quick->children.size() == 1);
    CHECK(quick->children[0].name == QStringLiteral("cancel"));
    CHECK(quick->children[0].type == QStringLiteral("tap"));

    const ComponentEntry* quicker = library.find(QStringLiteral("mine/quicker"));
    REQUIRE(quicker != nullptr);
    CHECK(quicker->type == QStringLiteral("hold"));
    CHECK(quicker->parent == QStringLiteral("mine/quick"));
    REQUIRE(quicker->props.size() == 6);
    CHECK(quicker->props[0].name == QStringLiteral("boost"));
    CHECK(quicker->props[1].name == QStringLiteral("times"));
    CHECK(quicker->props[1].value.toDouble() == 5.0);
    CHECK(quicker->props[2].name == QStringLiteral("word"));
    CHECK(quicker->props[3].of == QStringLiteral("skill"));
    CHECK_FALSE(quicker->props[3].value.isValid());
    CHECK(quicker->props[4].of.isEmpty());
    CHECK(quicker->props[5].value.toString() == QStringLiteral("jump"));
    REQUIRE(quicker->children.size() == 3);
    CHECK(quicker->children[0].name == QStringLiteral("cancel"));
    CHECK(quicker->children[1].name == QStringLiteral("skills.0"));
    CHECK(quicker->children[1].type == QStringLiteral("skill"));
    CHECK(quicker->children[2].name == QStringLiteral("skills.2"));

    CHECK(library.find(QStringLiteral("mine/plain"))->type.isEmpty());
    CHECK(library.find(QStringLiteral("mine/named"))->type == QStringLiteral("wasd"));
    CHECK(library.find(QStringLiteral("mine/named"))->className == QStringLiteral("Named"));
    CHECK(library.find(QStringLiteral("mine/loop-a"))->type.isEmpty());
    CHECK(library.find(QStringLiteral("mine/orphan"))->type.isEmpty());
    CHECK(library.find(QStringLiteral("mine/orphan"))->parent == QStringLiteral("mine/gone"));
    CHECK(library.extends(QStringLiteral("mine/quicker"), QStringLiteral("mine/quick")));
    CHECK(library.extends(QStringLiteral("mine/quick"), QStringLiteral("mine/quick")));
    CHECK_FALSE(library.extends(QStringLiteral("mine/quick"), QStringLiteral("mine/quicker")));
    CHECK_FALSE(library.extends(QStringLiteral("mine/loop-a"), QStringLiteral("mine/plain")));
    CHECK(library.info(QStringLiteral("mine/quicker")).value(QStringLiteral("type")).toString() == QStringLiteral("hold"));
}

TEST_CASE("ADR-0091: a new component starts from scratch, from a built-in component or from one of mine")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const std::filesystem::path base = pathOf(root);
    ComponentLibrary library(base / "examples", base / "mine");
    const auto starter = [&library](const QString& ref) {
        REQUIRE(library.find(ref) != nullptr);
        return readLines(pathOf(library.find(ref)->folder) / "component.ts");
    };

    const QString hold = library.add(QStringLiteral("Quick hold"), QStringLiteral("hold"));
    REQUIRE(hold == QStringLiteral("mine/quick-hold"));
    CHECK(starter(hold) == readLines(startersFolder() / "quick-hold" / "component.ts"));
    CHECK(library.find(hold)->type == QStringLiteral("hold"));
    CHECK(readLines(pathOf(library.find(hold)->folder) / "tsconfig.json") == gs::script::profileTsconfig());

    const QString jump = library.add(QStringLiteral("Jump"), QStringLiteral("tap"));
    CHECK(starter(jump) == readLines(startersFolder() / "jump" / "component.ts"));

    const QString combo = library.add(QStringLiteral("Combo"));
    CHECK(starter(combo) == readLines(startersFolder() / "combo" / "component.ts"));

    const QString plus = library.add(QStringLiteral("Quick hold plus"), hold);
    REQUIRE(plus == QStringLiteral("mine/quick-hold-plus"));
    CHECK(starter(plus) == readLines(startersFolder() / "quick-hold-plus" / "component.ts"));
    CHECK(library.find(plus)->parent == hold);
    CHECK(library.find(plus)->type == QStringLiteral("hold"));

    const QString comboPlus = library.add(QStringLiteral("Combo plus"), combo);
    CHECK(starter(comboPlus) == readLines(startersFolder() / "combo-plus" / "component.ts"));
    CHECK(library.find(comboPlus)->type.isEmpty());
    REQUIRE(library.find(comboPlus)->props.size() == 1);
    CHECK(library.find(comboPlus)->props[0].name == QStringLiteral("key"));

    const QString camera = library.add(QStringLiteral("Hold"), QStringLiteral("camera"));
    CHECK(starter(camera).starts_with("export default class MyHold extends Camera {\n    onTurnStart() {\n"));
    CHECK(library.add(QStringLiteral("Nope"), QStringLiteral("lever")).isEmpty());
    CHECK(library.add(QStringLiteral("Nope"), QStringLiteral("mine/none")).isEmpty());
    CHECK(library.mine().size() == 6);
}

TEST_CASE("ADR-0091: the starter components that come with the tests are read as they are written")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    ComponentLibrary library(pathOf(root) / "examples", startersFolder());
    const ComponentEntry* volley = library.find(QStringLiteral("mine/volley"));
    REQUIRE(volley != nullptr);
    CHECK(volley->type.isEmpty());
    REQUIRE(volley->children.size() == 3);
    CHECK(volley->children[0].name == QStringLiteral("cancel"));
    CHECK(volley->children[2].name == QStringLiteral("skills.1"));
    CHECK(volley->children[2].type == QStringLiteral("skill"));
    REQUIRE(volley->props.size() == 2);
    CHECK(volley->props[1].of == QStringLiteral("skill"));
    CHECK(library.find(QStringLiteral("mine/quick-hold-plus"))->parent == QStringLiteral("mine/quick-hold"));
    CHECK(library.find(QStringLiteral("mine/quick-hold-plus"))->type == QStringLiteral("hold"));
    CHECK(library.find(QStringLiteral("mine/jump"))->type == QStringLiteral("tap"));
}
