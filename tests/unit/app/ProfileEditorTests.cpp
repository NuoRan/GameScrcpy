#include "ComponentDefaults.h"
#include "ComponentLibrary.h"
#include "ProfileEditor.h"

#include <doctest/doctest.h>

#include <windows.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QVariantMap>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <variant>

using gs::app::ProfileEditor;
using namespace gs::input;

namespace {

const ScreenGeometry kScreen{.width = 2400, .height = 1080};

Vec2 centerOf(const ProfileEditor& editor, std::size_t index)
{
    const Control& control = editor.profile().controls.at(index);
    return CoordinateMapper(kScreen, ScaleMode::ShortSide, SafeArea::Ignore).position(control.anchor, control.offset);
}

Profile loaded(const char* json)
{
    LoadResult result = loadProfile(json);
    REQUIRE(result.ok());
    return result.profile;
}

QVariantList keyFieldsOf(const QVariantList& groups)
{
    QVariantList keys;
    for (const QVariant& group : groups) {
        for (const QVariant& field : group.toMap().value(QStringLiteral("fields")).toList()) {
            if (field.toMap().value(QStringLiteral("widget")).toString() == QStringLiteral("key")) {
                keys.append(field);
            }
        }
    }
    return keys;
}

std::optional<Binding> pressIn(const ProfileEditor& editor, std::size_t index)
{
    return std::visit(
        [](const auto& kind) -> std::optional<Binding> {
            if constexpr (requires { kind.press; }) {
                return kind.press;
            } else {
                return std::nullopt;
            }
        },
        editor.profile().controls.at(index).kind);
}

QString idAt(const ProfileEditor& editor, std::size_t index)
{
    return QString::fromStdString(editor.profile().controls.at(index).id);
}

QByteArray readAll(const QString& folder, const QString& name)
{
    QFile file(QDir(folder).filePath(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

void writeAll(const QString& folder, const QString& name, const QByteArray& contents)
{
    QDir().mkpath(QFileInfo(QDir(folder).filePath(name)).path());
    QFile file(QDir(folder).filePath(name));
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(file.write(contents) == contents.size());
}

}

TEST_CASE("profile editor: an added control takes the anchor of its third, a free id and the selection")
{
    ProfileEditor editor(Profile{}, kScreen);
    CHECK_FALSE(editor.dirty());
    CHECK_FALSE(editor.canUndo());

    const QString id = editor.addControl(QStringLiteral("hold"), 0.9, 0.85);
    CHECK(id == QStringLiteral("hold"));
    CHECK(editor.selection() == QStringList{id});
    REQUIRE(editor.profile().controls.size() == 1);
    CHECK(editor.profile().controls[0].anchor == Anchor::BottomRight);
    CHECK(centerOf(editor, 0).x == doctest::Approx(0.9));
    CHECK(centerOf(editor, 0).y == doctest::Approx(0.85));
    CHECK(editor.addControl(QStringLiteral("hold"), 0.1, 0.1) == QStringLiteral("hold2"));
    CHECK(editor.profile().controls[1].anchor == Anchor::TopLeft);
    CHECK(editor.addControl(QStringLiteral("nonsense"), 0.5, 0.5).isEmpty());
    CHECK(editor.addControl(QStringLiteral("button"), 0.5, 0.5).isEmpty());

    const QString look = editor.addControl(QStringLiteral("camera"), 0.7, 0.4);
    CHECK(editor.value(look, QStringLiteral("lockKey")).toString() == QStringLiteral("Backquote"));
    CHECK(editor.dirty());
}

TEST_CASE("profile editor: every component type can be added by its type name (ADR-0091)")
{
    ProfileEditor editor(Profile{}, kScreen);
    for (std::size_t i = 0; i < kControlTypeNames.size(); ++i) {
        const QString type = QString::fromLatin1(kControlTypeNames[i].data(), static_cast<qsizetype>(kControlTypeNames[i].size()));
        CAPTURE(type.toStdString());
        if (type == QStringLiteral("component") || type == QStringLiteral("behavior")) {
            CHECK(editor.addControl(type, 0.5, 0.5).isEmpty());
            continue;
        }
        CHECK_FALSE(editor.addControl(type, 0.5, 0.5).isEmpty());
        CHECK(editor.profile().controls.back().kind.index() == i);
        CHECK(editor.componentOf(QString::fromStdString(editor.profile().controls.back().id)) == type);
    }
    CHECK(loadProfile(saveProfile(editor.profile())).ok());
}

TEST_CASE("profile editor: undo and redo walk the history, and saving marks the clean point")
{
    ProfileEditor editor(Profile{}, kScreen);
    editor.addControl(QStringLiteral("hold"), 0.5, 0.5);
    editor.addControl(QStringLiteral("hold"), 0.6, 0.5);
    editor.markSaved();
    CHECK_FALSE(editor.dirty());

    editor.undo();
    CHECK(editor.profile().controls.size() == 1);
    CHECK(editor.dirty());
    CHECK(editor.canRedo());
    editor.redo();
    CHECK(editor.profile().controls.size() == 2);
    CHECK_FALSE(editor.dirty());
    CHECK_FALSE(editor.canRedo());

    editor.undo();
    editor.addControl(QStringLiteral("wasd"), 0.2, 0.7);
    CHECK_FALSE(editor.canRedo());
    CHECK(editor.dirty());
}

TEST_CASE("profile editor: undo brings back the selection a step was made with, redo the one it left")
{
    ProfileEditor editor(Profile{}, kScreen);
    const QString stick = editor.addControl(QStringLiteral("wasd"), 0.2, 0.7);
    const QString jump = editor.addControl(QStringLiteral("hold"), 0.8, 0.8);

    editor.select({stick});
    editor.removeSelection();
    CHECK(editor.selection().isEmpty());
    editor.undo();
    CHECK(editor.selection() == QStringList{stick});

    editor.select({jump});
    editor.moveSelection(0.05, 0.0);
    editor.select({stick});
    editor.undo();
    CHECK(editor.selection() == QStringList{jump});
    editor.select({});
    editor.redo();
    CHECK(editor.selection() == QStringList{jump});

    editor.select({stick});
    editor.removeSelection();
    editor.undo();
    CHECK(editor.selection() == QStringList{stick});
    editor.redo();
    CHECK(editor.selection().isEmpty());

    editor.select({jump});
    const QStringList copies = editor.duplicateSelection();
    REQUIRE(copies.size() == 1);
    CHECK(editor.selection() == copies);
    editor.undo();
    CHECK(editor.selection() == QStringList{jump});
    editor.redo();
    CHECK(editor.selection() == copies);
}

TEST_CASE("profile editor: copied keys paste around the given point, as one undo step, also into another editor (issue 18)")
{
    ProfileEditor editor(Profile{}, kScreen);
    editor.addControl(QStringLiteral("tap"), 0.2, 0.2);
    editor.addControl(QStringLiteral("tap"), 0.4, 0.2);
    const QString first = QString::fromStdString(editor.profile().controls[0].id);
    const QString second = QString::fromStdString(editor.profile().controls[1].id);
    editor.select({first, second});
    REQUIRE(editor.copySelection());
    REQUIRE(editor.canPaste());

    const QStringList pasted = editor.pasteAt(0.5, 0.5);
    REQUIRE(pasted.size() == 2);
    CHECK(editor.selection() == pasted);
    REQUIRE(editor.profile().controls.size() == 4);
    CHECK(centerOf(editor, 2).x == doctest::Approx(0.4));
    CHECK(centerOf(editor, 3).x == doctest::Approx(0.6));
    CHECK(centerOf(editor, 2).y == doctest::Approx(0.5));
    CHECK_FALSE(pasted.contains(first));

    editor.undo();
    CHECK(editor.profile().controls.size() == 2);

    ProfileEditor other(Profile{}, kScreen);
    CHECK(other.pasteAt(0.5, 0.5).size() == 2);
    CHECK(other.profile().controls.size() == 2);
}

TEST_CASE("profile editor: one drag is one undo step, separate moves are separate steps")
{
    ProfileEditor editor(Profile{}, kScreen);
    editor.addControl(QStringLiteral("wasd"), 0.2, 0.7);
    editor.beginGesture();
    for (int i = 0; i < 10; ++i) {
        editor.moveSelection(0.01, 0.0);
    }
    editor.endGesture();
    CHECK(centerOf(editor, 0).x == doctest::Approx(0.3));
    CHECK(centerOf(editor, 0).y == doctest::Approx(0.7));
    editor.undo();
    CHECK(centerOf(editor, 0).x == doctest::Approx(0.2));
    editor.redo();

    editor.moveSelection(0.05, 0.0);
    editor.moveSelection(0.05, 0.0);
    editor.undo();
    CHECK(centerOf(editor, 0).x == doctest::Approx(0.35));
    CHECK(editor.profile().controls[0].anchor == Anchor::Bottom);

    editor.placeControl(QStringLiteral("wasd"), 0.95, 1.4);
    CHECK(centerOf(editor, 0).x == doctest::Approx(0.95));
    CHECK(centerOf(editor, 0).y == doctest::Approx(1.0));
    CHECK(editor.profile().controls[0].anchor == Anchor::BottomRight);
}

TEST_CASE("profile editor: fields change through their profile paths and invalid values are refused")
{
    ProfileEditor editor(Profile{}, kScreen);
    QString stick = editor.addControl(QStringLiteral("wasd"), 0.2, 0.7);
    const auto renamed = [&] { return idAt(editor, 0); };

    CHECK(editor.setValue(stick, QStringLiteral("radius"), 0.2));
    CHECK(std::get<WasdControl>(editor.profile().controls[0].kind).radius == doctest::Approx(0.2));
    const int before = editor.revision();
    CHECK_FALSE(editor.setValue(stick, QStringLiteral("radius"), 5.0));
    CHECK(editor.revision() == before);

    CHECK(editor.setValue(stick, QStringLiteral("inputs.up"), QStringLiteral("KeyW")));
    stick = renamed();
    CHECK(stick == QStringLiteral("moveW"));
    CHECK(editor.value(stick, QStringLiteral("inputs.up")).toString() == QStringLiteral("KeyW"));
    const QVariantMap claimed{{QStringLiteral("input"), QStringLiteral("Mouse.X1")},
                              {QStringLiteral("claim"), QStringLiteral("both")}};
    CHECK(editor.setValue(stick, QStringLiteral("inputs.up"), claimed));
    stick = renamed();
    CHECK(stick == QStringLiteral("moveMouseX1"));
    CHECK(editor.value(stick, QStringLiteral("inputs.up.claim")).toString() == QStringLiteral("both"));
    const QVariantMap triggered{{QStringLiteral("input"), QStringLiteral("KeyI")},
                                {QStringLiteral("trigger"), QStringLiteral("toggle")}};
    CHECK(editor.setValue(stick, QStringLiteral("inputs.up"), triggered));
    stick = renamed();
    CHECK(stick == QStringLiteral("moveI"));
    CHECK_FALSE(editor.value(stick, QStringLiteral("inputs.up.trigger")).isValid());
    CHECK_FALSE(editor.setValue(stick, QStringLiteral("inputs.up"), QStringLiteral("NotAKey")));
    CHECK(editor.setValue(stick, QStringLiteral("inputs.up"), QVariant()));
    stick = renamed();
    CHECK(stick == QStringLiteral("moveI"));
    CHECK_FALSE(editor.value(stick, QStringLiteral("inputs.up")).isValid());

    CHECK(editor.setValue(stick, QStringLiteral("enabled"), false));
    CHECK_FALSE(editor.profile().controls[0].enabled);
    CHECK(editor.setValue(stick, QStringLiteral("offset.0"), 0.25));
    CHECK(editor.profile().controls[0].offset.x == doctest::Approx(0.25));
    CHECK_FALSE(editor.setValue(QStringLiteral("missing"), QStringLiteral("radius"), 0.2));
}

TEST_CASE("profile editor: conflicts are counted per input and can be selected together")
{
    ProfileEditor editor(Profile{}, kScreen);
    QString reload = editor.addControl(QStringLiteral("tap"), 0.8, 0.8);
    QString crouch = editor.addControl(QStringLiteral("hold"), 0.7, 0.8);
    CHECK(editor.setValue(reload, QStringLiteral("inputs.press"), QStringLiteral("KeyR")));
    reload = idAt(editor, 0);
    CHECK(editor.conflictCount() == 0);
    CHECK(editor.setValue(crouch, QStringLiteral("inputs.press"), QStringLiteral("KeyR")));
    crouch = idAt(editor, 1);
    CHECK(editor.conflictCount() == 1);
    editor.select({});
    editor.selectConflicts();
    CHECK(editor.selection() == QStringList{reload, crouch});

    const QVariantList keys = editor.keyList();
    REQUIRE(keys.size() == 2);
    CHECK(keys[0].toMap().value(QStringLiteral("conflict")).toBool());
    const QVariantList markers = editor.markers(2400, 1080);
    REQUIRE(markers.size() == 2);
    CHECK(markers[0].toMap().value(QStringLiteral("id")).toString() == reload);
    CHECK(markers[0].toMap().value(QStringLiteral("selected")).toBool());
    CHECK(markers[1].toMap().value(QStringLiteral("conflict")).toBool());

    CHECK(editor.setValue(crouch, QStringLiteral("enabled"), false));
    CHECK(editor.conflictCount() == 0);
}

TEST_CASE("profile editor: each conflicting input is a group of its own, numbered in the markers and the key list "
          "(issue 14)")
{
    ProfileEditor editor(Profile{}, kScreen);
    QString reload = editor.addControl(QStringLiteral("tap"), 0.8, 0.8);
    QString crouch = editor.addControl(QStringLiteral("hold"), 0.7, 0.8);
    QString jump = editor.addControl(QStringLiteral("tap"), 0.6, 0.8);
    QString dash = editor.addControl(QStringLiteral("hold"), 0.5, 0.8);
    QString lone = editor.addControl(QStringLiteral("tap"), 0.4, 0.8);
    CHECK(editor.setValue(reload, QStringLiteral("inputs.press"), QStringLiteral("KeyR")));
    reload = idAt(editor, 0);
    CHECK(editor.setValue(crouch, QStringLiteral("inputs.press"), QStringLiteral("KeyR")));
    crouch = idAt(editor, 1);
    CHECK(editor.setValue(jump, QStringLiteral("inputs.press"), QStringLiteral("Space")));
    jump = idAt(editor, 2);
    CHECK(editor.setValue(dash, QStringLiteral("inputs.press"), QStringLiteral("Space")));
    dash = idAt(editor, 3);
    CHECK(editor.setValue(lone, QStringLiteral("inputs.press"), QStringLiteral("KeyF")));
    lone = idAt(editor, 4);
    REQUIRE(editor.conflictCount() == 2);

    const QHash<QString, int> groups = editor.conflictGroups();
    CHECK(groups.size() == 4);
    CHECK(groups.value(reload) == groups.value(crouch));
    CHECK(groups.value(jump) == groups.value(dash));
    CHECK(groups.value(reload) != groups.value(jump));
    CHECK_FALSE(groups.contains(lone));

    const auto groupOf = [](const QVariantList& items, const QString& id) {
        for (const QVariant& item : items) {
            if (item.toMap().value(QStringLiteral("id")).toString() == id) {
                return item.toMap().value(QStringLiteral("conflictGroup")).toInt();
            }
        }
        return -2;
    };
    const QVariantList markers = editor.markers(2400, 1080);
    const QVariantList keys = editor.keyList();
    for (const QString& id : {reload, crouch, jump, dash}) {
        CHECK(groupOf(markers, id) == groups.value(id));
        CHECK(groupOf(keys, id) == groups.value(id));
    }
    CHECK(groupOf(markers, lone) == -1);
    CHECK(groupOf(keys, lone) == -1);
}

TEST_CASE("profile editor: removing a control drops what pointed at it; copies get new ids beside the originals")
{
    ProfileEditor editor(loaded(R"({"format":"gamescrcpy.profile","version":3,
        "controls": [
            {"id": "move", "type": "rightWalk", "anchor": "bottomLeft", "offset": [0.3, -0.3]},
            {"id": "skill", "type": "skill", "inputs": {"press": "KeyQ"}, "pauses": "move"}
        ]})"),
                         kScreen);
    editor.select({QStringLiteral("skill")});
    const QStringList copies = editor.duplicateSelection();
    REQUIRE(copies == QStringList{QStringLiteral("skill2")});
    CHECK(editor.selection() == copies);
    CHECK(centerOf(editor, 2).x == doctest::Approx(centerOf(editor, 1).x + 0.03));

    editor.select({QStringLiteral("move")});
    editor.removeSelection();
    CHECK(editor.selection().isEmpty());
    REQUIRE(editor.profile().controls.size() == 2);
    CHECK(std::get<SkillControl>(editor.profile().controls[0].kind).pauses.empty());
    CHECK(std::get<SkillControl>(editor.profile().controls[1].kind).pauses.empty());
    CHECK(loadProfile(saveProfile(editor.profile())).ok());
}

TEST_CASE("profile editor: ranges dragged on the picture are stored in short-side units")
{
    ProfileEditor editor(Profile{}, kScreen);
    const QString stick = editor.addControl(QStringLiteral("wasd"), 0.2, 0.7);
    CHECK(editor.setLength(stick, QStringLiteral("radius"), 0.1));
    CHECK(std::get<WasdControl>(editor.profile().controls[0].kind).radius == doctest::Approx(0.1 * 2400 / 1080));

    const QString look = editor.addControl(QStringLiteral("camera"), 0.7, 0.4);
    CHECK(editor.setArea(look, QStringLiteral("size"), 0.5, 0.4));
    const Vec2 size = std::get<CameraControl>(editor.profile().controls[1].kind).size;
    CHECK(size.x == doctest::Approx(0.5 * 2400 / 1080));
    CHECK(size.y == doctest::Approx(0.4));

    const QString skill = editor.addControl(QStringLiteral("skill"), 0.8, 0.8);
    CHECK(editor.value(skill, QStringLiteral("direction")).toString() == QStringLiteral("cursor"));
    CHECK(editor.setPoint(skill, QStringLiteral("hero"), 0.6, 0.5));
    const Vec2 hero = std::get<SkillControl>(editor.profile().controls[2].kind).hero;
    CHECK(hero.x == doctest::Approx(240.0 / 1080));
    CHECK(hero.y == doctest::Approx(0.0));
    CHECK(editor.setLength(skill, QStringLiteral("castRadius"), 0.2));
    CHECK_FALSE(editor.setLength(stick, QStringLiteral("radius"), 5.0));
}

TEST_CASE("profile editor: several keys align on an edge or a center line and spread evenly")
{
    ProfileEditor editor(Profile{}, kScreen);
    editor.addControl(QStringLiteral("hold"), 0.2, 0.3);
    editor.addControl(QStringLiteral("hold"), 0.5, 0.5);
    editor.addControl(QStringLiteral("hold"), 0.8, 0.4);
    editor.selectAll();
    CHECK(editor.selection().size() == 3);

    editor.alignSelection(QStringLiteral("top"));
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(centerOf(editor, i).y == doctest::Approx(0.3));
    }
    editor.alignSelection(QStringLiteral("centerX"));
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(centerOf(editor, i).x == doctest::Approx(0.5));
    }
    editor.undo();
    CHECK(centerOf(editor, 0).x == doctest::Approx(0.2));

    editor.placeControl(QStringLiteral("hold2"), 0.25, 0.3);
    editor.distributeSelection(true);
    CHECK(centerOf(editor, 1).x == doctest::Approx(0.5));
    editor.select({QStringLiteral("hold"), QStringLiteral("hold2")});
    const int before = editor.revision();
    editor.distributeSelection(true);
    CHECK(editor.revision() == before);
}

TEST_CASE("profile editor: markers show every control, unbound ones with a placeholder and disabled ones dimmed")
{
    ProfileEditor editor(Profile{}, kScreen);
    const QString spare = editor.addControl(QStringLiteral("tap"), 0.5, 0.5);
    QString fire = editor.addControl(QStringLiteral("hold"), 0.7, 0.5);
    CHECK(editor.setValue(fire, QStringLiteral("inputs.press"), QStringLiteral("KeyF")));
    fire = idAt(editor, 1);
    CHECK(fire == QStringLiteral("holdF"));
    CHECK(editor.setValue(fire, QStringLiteral("enabled"), false));
    const QVariantList markers = editor.markers(2400, 1080);
    REQUIRE(markers.size() == 2);
    const QVariantMap unbound = markers[0].toMap();
    CHECK(unbound.value(QStringLiteral("id")).toString() == spare);
    CHECK(unbound.value(QStringLiteral("type")).toString() == QStringLiteral("tap"));
    CHECK(unbound.value(QStringLiteral("unbound")).toBool());
    CHECK(unbound.value(QStringLiteral("live")).toBool());
    CHECK(unbound.value(QStringLiteral("keys")).toList().first().toMap().value(QStringLiteral("text")) ==
          QStringLiteral("?"));
    const QVariantMap disabled = markers[1].toMap();
    CHECK(disabled.value(QStringLiteral("type")).toString() == QStringLiteral("hold"));
    CHECK(disabled.value(QStringLiteral("dim")).toBool());
    CHECK(disabled.value(QStringLiteral("live")).toBool());
    CHECK_FALSE(disabled.value(QStringLiteral("unbound")).toBool());
    CHECK(editor.keyList().size() == 2);
}

TEST_CASE("profile editor: swipe path points come with the marker and move one at a time")
{
    ProfileEditor editor(Profile{}, kScreen);
    const QString swipe = editor.addControl(QStringLiteral("swipe"), 0.5, 0.5);
    const auto pathOf = [&] { return editor.markers(2400, 1080).at(0).toMap().value(QStringLiteral("path")).toList(); };
    const auto pointOf = [&](int index) { return pathOf().at(index).toMap(); };
    REQUIRE(pathOf().size() == 2);
    CHECK(pointOf(0).value(QStringLiteral("x")).toDouble() == doctest::Approx(1200));
    CHECK(pointOf(1).value(QStringLiteral("x")).toDouble() == doctest::Approx(1200 + 0.2 * 1080));
    CHECK(pointOf(1).value(QStringLiteral("y")).toDouble() == doctest::Approx(540));

    CHECK(editor.setPathPoint(swipe, 1, 0.5, 0.25));
    CHECK(pointOf(1).value(QStringLiteral("x")).toDouble() == doctest::Approx(1200));
    CHECK(pointOf(1).value(QStringLiteral("y")).toDouble() == doctest::Approx(270));
    const Vec2 end = std::get<SwipeControl>(editor.profile().controls[0].kind).path[1];
    CHECK(end.x == doctest::Approx(0.0));
    CHECK(end.y == doctest::Approx(-0.25));
    CHECK(pointOf(0).value(QStringLiteral("x")).toDouble() == doctest::Approx(1200));

    CHECK_FALSE(editor.setPathPoint(swipe, 2, 0.5, 0.5));
    CHECK_FALSE(editor.setPathPoint(QStringLiteral("missing"), 0, 0.5, 0.5));
    editor.undo();
    CHECK(pointOf(1).value(QStringLiteral("y")).toDouble() == doctest::Approx(540));
}

TEST_CASE("profile editor: an action wheel's cells change count, name, action, component and tap spot (ADR-0094)")
{
    ProfileEditor editor(Profile{}, kScreen);
    const QString potion = editor.addControl(QStringLiteral("tap"), 0.2, 0.2);
    const QVariantMap added = editor.addComponent(QStringLiteral("actionWheel"), 0.8, 0.8);
    const QString id = added.value(QStringLiteral("id")).toString();
    CHECK(id == QStringLiteral("wheel"));
    CHECK(added.value(QStringLiteral("slot")).toString() == QStringLiteral("inputs.press"));
    CHECK_FALSE(editor.profile().controls.back().name.empty());
    CHECK(editor.wheelCells(id).size() == 8);
    CHECK(editor.setWheelCellCount(id, 3));
    CHECK(editor.wheelCells(id).size() == 3);
    CHECK(editor.setWheelCellCount(id, 1));
    CHECK(editor.wheelCells(id).size() == 2);
    CHECK(editor.setWheelCellCount(id, 4));

    CHECK(editor.setWheelCell(id, 0, QStringLiteral("name"), QStringLiteral("  potion  ")));
    CHECK(editor.setWheelCell(id, 0, QStringLiteral("action"), QStringLiteral("control")));
    CHECK(editor.setWheelCell(id, 0, QStringLiteral("control"), potion));
    CHECK_FALSE(editor.setWheelCell(id, 0, QStringLiteral("control"), QStringLiteral("nothing")));
    CHECK_FALSE(editor.setWheelCell(id, 0, QStringLiteral("action"), QStringLiteral("jump")));
    CHECK_FALSE(editor.setWheelCell(id, 9, QStringLiteral("name"), QStringLiteral("x")));
    const QVariantMap first = editor.wheelCells(id).at(0).toMap();
    CHECK(first.value(QStringLiteral("name")).toString() == QStringLiteral("potion"));
    CHECK(first.value(QStringLiteral("action")).toString() == QStringLiteral("control"));
    CHECK(first.value(QStringLiteral("control")).toString() == potion);
    const QVariantList targets = editor.wheelTargets(id);
    REQUIRE(targets.size() == 1);
    CHECK(targets.at(0).toMap().value(QStringLiteral("id")).toString() == potion);

    CHECK(editor.setWheelCell(id, 1, QStringLiteral("action"), QStringLiteral("tap")));
    CHECK(editor.setPlacedPoint(id, QStringLiteral("cells.1"), 0.9, 0.8));
    CHECK(std::get<ActionWheelControl>(editor.profile().controls.back().kind).cells[1].at.x > 0.0);
    CHECK_FALSE(editor.setPlacedPoint(id, QStringLiteral("cells.7"), 0.9, 0.8));
    CHECK(loadProfile(saveProfile(editor.profile())).ok());
}

TEST_CASE("profile editor: library components come with free default keys and the slot to bind (ADR-0091)")
{
    ProfileEditor editor(Profile{}, kScreen);
    const auto chord = [](const char* text) { return *parseChord(text); };
    const auto controlAt = [&](std::size_t index) -> const Control& { return editor.profile().controls.at(index); };

    const QVariantMap tap = editor.addComponent(QStringLiteral("tap"), 0.8, 0.8);
    CHECK(tap.value(QStringLiteral("id")).toString() == QStringLiteral("tap"));
    CHECK(tap.value(QStringLiteral("slot")).toString() == QStringLiteral("inputs.press"));
    CHECK_FALSE(tap.contains(QStringLiteral("trigger")));
    CHECK(editor.selection() == QStringList{QStringLiteral("tap")});
    CHECK(editor.bindInput(QStringLiteral("tap"), QStringLiteral("inputs.press"), QStringLiteral("Mouse.Left")));
    CHECK(std::get<TapControl>(controlAt(0).kind).press->input == chord("Mouse.Left"));

    const QVariantMap hold = editor.addComponent(QStringLiteral("longPress"), 0.7, 0.8);
    CHECK(hold.value(QStringLiteral("id")).toString() == QStringLiteral("long"));
    CHECK(editor.bindInput(hold.value(QStringLiteral("id")).toString(), QStringLiteral("inputs.press"),
                           QStringLiteral("KeyE")));
    CHECK(std::get<LongPressControl>(controlAt(1).kind).press->input == chord("KeyE"));
    CHECK(editor.addComponent(QStringLiteral("turbo"), 0.6, 0.8).value(QStringLiteral("slot")).toString() ==
          QStringLiteral("inputs.press"));
    CHECK(std::holds_alternative<TurboControl>(controlAt(2).kind));

    const QVariantMap stick = editor.addComponent(QStringLiteral("wasd"), 0.2, 0.7);
    CHECK(stick.value(QStringLiteral("id")).toString() == QStringLiteral("moveWASD"));
    CHECK(stick.value(QStringLiteral("slot")).toString().isEmpty());
    const auto& move = std::get<WasdControl>(controlAt(3).kind);
    CHECK(move.up->input == chord("KeyW"));
    CHECK(move.right->input == chord("KeyD"));
    editor.addComponent(QStringLiteral("wasd"), 0.3, 0.7);
    CHECK_FALSE(std::get<WasdControl>(controlAt(4).kind).up);
    CHECK(editor.conflictCount() == 0);

    editor.addComponent(QStringLiteral("camera"), 0.7, 0.4);
    CHECK(std::get<CameraControl>(controlAt(5).kind).look->input == chord("Mouse.Move"));
    CHECK(std::get<CameraControl>(controlAt(5).kind).lockKey == chord("Backquote"));
    editor.addComponent(QStringLiteral("freeLook"), 0.8, 0.2);
    CHECK(std::get<FreeLookControl>(controlAt(6).kind).hold->input == chord("AltLeft"));
    CHECK(std::get<FreeLookControl>(controlAt(6).kind).look->input == chord("Mouse.Move"));
    editor.addComponent(QStringLiteral("pinch"), 0.5, 0.5);
    CHECK(std::get<PinchControl>(controlAt(7).kind).zoomIn->input == chord("Mouse.WheelUp"));
    CHECK(editor.conflictCount() == 0);
    editor.addComponent(QStringLiteral("camera"), 0.5, 0.4);
    CHECK_FALSE(std::get<CameraControl>(controlAt(8).kind).look);
    CHECK(editor.conflictCount() == 1);

    const QVariantMap skill = editor.addComponent(QStringLiteral("skill"), 0.6, 0.6);
    CHECK(skill.value(QStringLiteral("slot")).toString() == QStringLiteral("inputs.press"));
    CHECK(std::get<SkillControl>(controlAt(9).kind).direction == AimDirection::Cursor);
    CHECK(editor.addComponent(QStringLiteral("swipe"), 0.4, 0.4).value(QStringLiteral("id")).toString() ==
          QStringLiteral("swipe"));

    const QVariantMap walk = editor.addComponent(QStringLiteral("rightWalk"), 0.2, 0.7);
    CHECK(walk.value(QStringLiteral("id")).toString() == QStringLiteral("walkMouseRight"));
    CHECK(std::get<RightWalkControl>(controlAt(11).kind).walkTo->input == chord("Mouse.Right"));
    editor.addComponent(QStringLiteral("padStick"), 0.2, 0.7);
    CHECK(std::get<PadStickControl>(controlAt(12).kind).stick->input == chord("Gamepad.LeftStick"));
    editor.addComponent(QStringLiteral("padCamera"), 0.7, 0.4);
    CHECK(std::get<PadCameraControl>(controlAt(13).kind).look->input == chord("Gamepad.RightStick"));
    editor.addComponent(QStringLiteral("hold"), 0.5, 0.2);
    CHECK(std::holds_alternative<HoldControl>(controlAt(14).kind));

    CHECK(editor.addComponent(QStringLiteral("nonsense"), 0.5, 0.5).isEmpty());
    CHECK(editor.addComponent(QStringLiteral("walk"), 0.5, 0.5).isEmpty());
    CHECK(editor.addComponent(QStringLiteral("joystick"), 0.5, 0.5).isEmpty());
}

TEST_CASE("profile editor: a key pressed while binding becomes the input name of its physical key")
{
    ProfileEditor editor(Profile{}, kScreen);
    CHECK(editor.inputOfKey(0x11, 0) == QStringLiteral("KeyW"));
    CHECK(editor.inputOfKey(0x11, Qt::ControlModifier) == QStringLiteral("Ctrl+KeyW"));
    CHECK(editor.inputOfKey(0x11, Qt::ControlModifier | Qt::ShiftModifier) == QStringLiteral("Ctrl+Shift+KeyW"));
    CHECK(editor.inputOfKey(0x38, Qt::AltModifier) == QStringLiteral("AltLeft"));
    CHECK(editor.inputOfKey(0xE038, Qt::AltModifier) == QStringLiteral("AltRight"));
    CHECK(editor.inputOfKey(0x39, 0) == QStringLiteral("Space"));
    CHECK(editor.inputOfKey(0, 0).isEmpty());
}

TEST_CASE("profile editor: new keys are named after their kind, copies are numbered and renaming touches only the name")
{
    ProfileEditor editor(Profile{}, kScreen);
    const QString tap = editor.addComponent(QStringLiteral("tap"), 0.8, 0.8).value(QStringLiteral("id")).toString();
    const QString second = editor.addComponent(QStringLiteral("tap"), 0.7, 0.8).value(QStringLiteral("id")).toString();
    const QString hold = editor.addComponent(QStringLiteral("longPress"), 0.6, 0.8).value(QStringLiteral("id")).toString();
    CHECK(editor.bindInput(hold, QStringLiteral("inputs.press"), QStringLiteral("KeyE")));
    editor.addComponent(QStringLiteral("rightWalk"), 0.2, 0.7);
    CHECK(editor.profile().controls[0].name == "Tap");
    CHECK(editor.profile().controls[1].name == "Tap 2");
    CHECK(editor.profile().controls[2].name == "Long press");
    CHECK(editor.profile().controls[3].name == "Right-click walk");

    editor.select({tap});
    const QStringList copies = editor.duplicateSelection();
    REQUIRE(copies.size() == 1);
    CHECK(editor.profile().controls[4].name == "Tap 3");
    editor.select({second});
    editor.duplicateSelection();
    CHECK(editor.profile().controls[5].name == "Tap 4");

    CHECK(editor.setValue(tap, QStringLiteral("inputs.press"), QStringLiteral("KeyF")));
    const QString tapped = QString::fromStdString(editor.profile().controls[0].id);
    CHECK(tapped == QStringLiteral("tapF"));
    CHECK(editor.rename(tapped, QStringLiteral("  开   火 ")));
    CHECK(editor.profile().controls[0].name == "开 火");
    CHECK(editor.profile().controls[0].id == tapped.toStdString());
    CHECK(editor.value(tapped, QStringLiteral("inputs.press")).toString() == QStringLiteral("KeyF"));
    const int before = editor.revision();
    CHECK_FALSE(editor.rename(tapped, QStringLiteral("   ")));
    CHECK_FALSE(editor.rename(tapped, QString(65, u'键')));
    CHECK_FALSE(editor.rename(QStringLiteral("missing"), QStringLiteral("x")));
    CHECK(editor.revision() == before);
    CHECK(editor.rename(tapped, QString(64, u'键')));
    editor.undo();
    CHECK(editor.profile().controls[0].name == "开 火");

    const QVariantList rows = editor.keyList();
    REQUIRE(rows.size() == 6);
    const QVariantMap first = rows[0].toMap();
    CHECK(first.value(QStringLiteral("name")).toString() == QStringLiteral("开 火"));
    CHECK(first.value(QStringLiteral("ordinal")).toInt() == 1);
    CHECK(rows[2].toMap().value(QStringLiteral("type")).toString() == QStringLiteral("longPress"));
    CHECK(rows[2].toMap().value(QStringLiteral("id")).toString() ==
          QString::fromStdString(editor.profile().controls[2].id));
    CHECK(rows[3].toMap().value(QStringLiteral("type")).toString() == QStringLiteral("rightWalk"));

    ProfileEditor imported(loaded(R"({"format":"gamescrcpy.profile","version":3,
        "controls": [{"id": "jump", "type": "tap", "inputs": {"press": "Space"}}]})"),
                           kScreen);
    CHECK(imported.keyList().at(0).toMap().value(QStringLiteral("name")).toString() == QStringLiteral("jump"));
    imported.select({QStringLiteral("jump")});
    imported.duplicateSelection();
    CHECK(imported.profile().controls[1].name.empty());

    ProfileEditor crowded(Profile{}, kScreen);
    const QString crowd = crowded.addComponent(QStringLiteral("tap"), 0.5, 0.5).value(QStringLiteral("id")).toString();
    CHECK(crowded.rename(crowd, QString(64, u'键')));
    crowded.select({crowd});
    crowded.duplicateSelection();
    CHECK(QString::fromStdString(crowded.profile().controls[1].name) == QString(62, u'键') + QStringLiteral(" 2"));
}

TEST_CASE("profile editor: keys change by slot on the picture, and the form lists every key of the component too (ADR-0091)")
{
    ProfileEditor editor(Profile{}, kScreen);
    const auto controlAt = [&](std::size_t index) -> const Control& { return editor.profile().controls.at(index); };

    const QString fire = editor.addComponent(QStringLiteral("turbo"), 0.8, 0.8).value(QStringLiteral("id")).toString();
    CHECK(fire == QStringLiteral("turbo"));
    CHECK(editor.bindInput(fire, QStringLiteral("inputs.press"), QStringLiteral("KeyR")));
    QString fireId = QString::fromStdString(editor.profile().controls[0].id);
    CHECK(fireId == QStringLiteral("turboR"));
    const QVariantList fireKeys = keyFieldsOf(editor.form(fireId));
    REQUIRE(fireKeys.size() == 1);
    CHECK(fireKeys[0].toMap().value(QStringLiteral("bindPath")).toString() == QStringLiteral("inputs.press"));
    CHECK_FALSE(fireKeys[0].toMap().value(QStringLiteral("keys")).toList().isEmpty());
    CHECK(editor.rebind(fireId, QStringLiteral("press"), QStringLiteral("Ctrl+Mouse.Right")));
    CHECK(std::get<TurboControl>(controlAt(0).kind).press->input == *parseChord("Ctrl+Mouse.Right"));
    fireId = QString::fromStdString(editor.profile().controls[0].id);
    CHECK(fireId == QStringLiteral("turboCtrlMouseRight"));
    const int before = editor.revision();
    CHECK_FALSE(editor.rebind(fireId, QStringLiteral("zoomIn"), QStringLiteral("KeyQ")));
    CHECK_FALSE(editor.rebind(fireId, QStringLiteral("press"), QString()));
    CHECK_FALSE(editor.rebind(QStringLiteral("missing"), QStringLiteral("press"), QStringLiteral("KeyQ")));
    CHECK(editor.revision() == before);

    const QString move = editor.addComponent(QStringLiteral("wasd"), 0.2, 0.7).value(QStringLiteral("id")).toString();
    CHECK(move == QStringLiteral("moveWASD"));
    CHECK(keyFieldsOf(editor.form(move)).size() == 6);
    CHECK(editor.rebind(move, QStringLiteral("up"), QStringLiteral("KeyI")));
    const QString moved = QString::fromStdString(editor.profile().controls[1].id);
    CHECK(moved == QStringLiteral("moveIASD"));
    CHECK(std::get<WasdControl>(controlAt(1).kind).up->input == *parseChord("KeyI"));
    CHECK(editor.rebind(moved, QStringLiteral("sprint"), QStringLiteral("ShiftLeft")));
    CHECK_FALSE(editor.rebind(moved, QStringLiteral("walkTo"), QStringLiteral("Mouse.Right")));
    CHECK_FALSE(editor.rebind(moved, QStringLiteral("stick"), QStringLiteral("Gamepad.LeftStick")));

    const QString look = editor.addComponent(QStringLiteral("camera"), 0.7, 0.4).value(QStringLiteral("id")).toString();
    CHECK(keyFieldsOf(editor.form(look)).isEmpty());
    CHECK(editor.rebind(look, QStringLiteral("lockKey"), QStringLiteral("KeyL")));
    CHECK(std::get<CameraControl>(controlAt(2).kind).lockKey == *parseChord("KeyL"));
    CHECK_FALSE(editor.rebind(look, QStringLiteral("look"), QStringLiteral("Mouse.Move")));

    const QString skill = editor.addComponent(QStringLiteral("skill"), 0.6, 0.6).value(QStringLiteral("id")).toString();
    const QVariantList skillKeys = keyFieldsOf(editor.form(skill));
    REQUIRE(skillKeys.size() == 3);
    CHECK(skillKeys[0].toMap().value(QStringLiteral("bindPath")).toString() == QStringLiteral("inputs.press"));
    CHECK(skillKeys[2].toMap().value(QStringLiteral("slot")).toString() == QStringLiteral("castKey"));
    const QVariantMap cancel = skillKeys[1].toMap();
    CHECK(cancel.value(QStringLiteral("slot")).toString() == QStringLiteral("cancel"));
    CHECK(cancel.value(QStringLiteral("widget")).toString() == QStringLiteral("key"));
    CHECK(cancel.value(QStringLiteral("keys")).toList().isEmpty());
    CHECK(cancel.value(QStringLiteral("bindPath")).toString() == QStringLiteral("inputs.cancel"));
    CHECK(cancel.value(QStringLiteral("clearPath")).toString() == QStringLiteral("inputs.cancel"));
    CHECK(editor.setKey(skill, cancel.value(QStringLiteral("bindPath")).toString(), QStringLiteral("Space")));
    CHECK(editor.rebind(skill, QStringLiteral("cancel"), QStringLiteral("KeyX")));
    CHECK(std::get<SkillControl>(controlAt(3).kind).cancel->input == *parseChord("KeyX"));

    const QString dodge = editor.addComponent(QStringLiteral("tap"), 0.4, 0.4).value(QStringLiteral("id")).toString();
    CHECK(editor.bindInput(dodge, QStringLiteral("inputs.press"), QStringLiteral("KeyX")));
    const QVariantMap cancelled = keyFieldsOf(editor.form(skill)).at(1).toMap();
    CHECK(cancelled.value(QStringLiteral("conflict")).toString() == QStringLiteral("Also bound to “Tap”"));
    CHECK(editor.form(QStringLiteral("missing")).isEmpty());
}

TEST_CASE("profile editor: every unbound main slot of a marker gets its own placeholder")
{
    ProfileEditor editor(loaded(R"({"format":"gamescrcpy.profile","version":3,
        "controls": [
            {"id": "move", "type": "wasd", "inputs": {"up": "KeyW", "sprint": "ShiftLeft"}},
            {"id": "zoom", "type": "pinch", "inputs": {"zoomIn": "Mouse.WheelUp"}},
            {"id": "jump", "type": "tap"},
            {"id": "walk", "type": "rightWalk"},
            {"id": "pad", "type": "padStick"}
        ]})"),
                         kScreen);
    const auto slotsOf = [&](int index) {
        QStringList names;
        for (const QVariant& key : editor.markers(2400, 1080).at(index).toMap().value(QStringLiteral("keys")).toList()) {
            const QVariantMap map = key.toMap();
            names.append(map.value(QStringLiteral("slot")).toString() + QLatin1Char(':') +
                         map.value(QStringLiteral("text")).toString());
        }
        return names;
    };
    const QStringList move = slotsOf(0);
    REQUIRE(move.size() == 5);
    CHECK_FALSE(move[0].endsWith(QStringLiteral(":?")));
    CHECK(move[1] == QStringLiteral("down:?"));
    CHECK(move[2] == QStringLiteral("left:?"));
    CHECK(move[3] == QStringLiteral("right:?"));
    CHECK(move[4].startsWith(QStringLiteral("sprint:")));
    const QStringList zoom = slotsOf(1);
    REQUIRE(zoom.size() == 2);
    CHECK(zoom[1] == QStringLiteral("zoomOut:?"));
    CHECK(slotsOf(2) == QStringList{QStringLiteral("press:?")});
    CHECK(slotsOf(3) == QStringList{QStringLiteral("walkTo:?")});
    CHECK(slotsOf(4).isEmpty());
    CHECK(editor.markers(2400, 1080).at(2).toMap().value(QStringLiteral("unbound")).toBool());
    CHECK(editor.markers(2400, 1080).at(4).toMap().value(QStringLiteral("unbound")).toBool());
    CHECK_FALSE(editor.markers(2400, 1080).at(0).toMap().value(QStringLiteral("unbound")).toBool());
}

TEST_CASE("profile editor: a long press or turbo placed before its key has none, and the key fills it in")
{
    ProfileEditor editor(Profile{}, kScreen);
    const QString hold =
        editor.addComponent(QStringLiteral("longPress"), 0.5, 0.5).value(QStringLiteral("id")).toString();
    CHECK_FALSE(pressIn(editor, 0).has_value());
    const QVariantMap marker = editor.markers(2400, 1080).at(0).toMap();
    CHECK(marker.value(QStringLiteral("unbound")).toBool());
    const QVariantList keys = marker.value(QStringLiteral("keys")).toList();
    REQUIRE(keys.size() == 1);
    CHECK(keys[0].toMap().value(QStringLiteral("slot")).toString() == QStringLiteral("press"));
    CHECK(keys[0].toMap().value(QStringLiteral("text")).toString() == QStringLiteral("?"));

    const QString fast = editor.addComponent(QStringLiteral("turbo"), 0.6, 0.5).value(QStringLiteral("id")).toString();
    CHECK_FALSE(pressIn(editor, 1).has_value());
    CHECK(editor.conflictingIds().isEmpty());
    CHECK(loadProfile(saveProfile(editor.profile())).profile == editor.profile());

    CHECK(editor.bindInput(hold, QStringLiteral("inputs.press"), QStringLiteral("KeyF")));
    CHECK(pressIn(editor, 0)->input == *parseChord("KeyF"));
    CHECK(std::holds_alternative<LongPressControl>(editor.profile().controls.at(0).kind));
    CHECK(editor.rebind(fast, QStringLiteral("press"), QStringLiteral("KeyG")));
    CHECK(pressIn(editor, 1)->input == *parseChord("KeyG"));
    CHECK(std::holds_alternative<TurboControl>(editor.profile().controls.at(1).kind));
    CHECK_FALSE(editor.markers(2400, 1080).at(0).toMap().value(QStringLiteral("unbound")).toBool());
}

TEST_CASE("profile editor: rebinding a mouse button to a key drops its claim, and a new skill casts with the left button")
{
    ProfileEditor editor(loaded(R"({"format":"gamescrcpy.profile","version":3,
        "controls": [{"id": "fire", "type": "tap",
                      "inputs": {"press": {"input": "Mouse.Right", "claim": "both"}}}]})"),
                         kScreen);
    const QString fire = QStringLiteral("fire");
    const auto pressOf = [&] { return *std::get<TapControl>(editor.profile().controls.at(0).kind).press; };
    CHECK(editor.rebind(fire, QStringLiteral("press"), QStringLiteral("Mouse.X1")));
    CHECK(pressOf().input == *parseChord("Mouse.X1"));
    CHECK(pressOf().claim == Claim::Both);
    const QString x1 = QString::fromStdString(editor.profile().controls[0].id);
    CHECK(x1 == QStringLiteral("tapMouseX1"));
    CHECK(editor.rebind(x1, QStringLiteral("press"), QStringLiteral("KeyQ")));
    CHECK(pressOf().input == *parseChord("KeyQ"));
    CHECK(pressOf().claim == Claim::Lock);
    CHECK_FALSE(editor.value(x1, QStringLiteral("inputs.press.claim")).isValid());

    const QString skill = editor.addComponent(QStringLiteral("skill"), 0.6, 0.6).value(QStringLiteral("id")).toString();
    const auto castOf = [&] { return std::get<SkillControl>(editor.profile().controls.at(1).kind).castKey; };
    REQUIRE(castOf().has_value());
    CHECK(castOf()->input == *parseChord("Mouse.Left"));
    CHECK(castOf()->claim == Claim::Cursor);
    CHECK(editor.value(skill, QStringLiteral("inputs.cast")).toString() == QStringLiteral("Mouse.Left"));
    CHECK(editor.rebind(skill, QStringLiteral("cast"), QStringLiteral("KeyF")));
    CHECK(castOf()->input == *parseChord("KeyF"));
}

TEST_CASE("profile editor: mouse buttons and the wheel become input names with their modifiers")
{
    ProfileEditor editor(Profile{}, kScreen);
    CHECK(editor.inputOfMouse(Qt::LeftButton, 0) == QStringLiteral("Mouse.Left"));
    CHECK(editor.inputOfMouse(Qt::RightButton, Qt::ControlModifier) == QStringLiteral("Ctrl+Mouse.Right"));
    CHECK(editor.inputOfMouse(Qt::MiddleButton, 0) == QStringLiteral("Mouse.Middle"));
    CHECK(editor.inputOfMouse(Qt::BackButton, 0) == QStringLiteral("Mouse.X1"));
    CHECK(editor.inputOfMouse(Qt::ForwardButton, Qt::ShiftModifier | Qt::AltModifier) ==
          QStringLiteral("Shift+Alt+Mouse.X2"));
    CHECK(editor.inputOfMouse(Qt::NoButton, 0).isEmpty());
    CHECK(editor.inputOfWheel(0, 120, 0) == QStringLiteral("Mouse.WheelUp"));
    CHECK(editor.inputOfWheel(10, -120, Qt::ShiftModifier) == QStringLiteral("Shift+Mouse.WheelDown"));
    CHECK(editor.inputOfWheel(120, 0, 0) == QStringLiteral("Mouse.WheelLeft"));
    CHECK(editor.inputOfWheel(-120, 0, 0) == QStringLiteral("Mouse.WheelRight"));
    CHECK(editor.inputOfWheel(0, 0, 0).isEmpty());
}

TEST_CASE("profile editor: the history keeps the latest steps only")
{
    ProfileEditor editor(Profile{}, kScreen);
    editor.addControl(QStringLiteral("hold"), 0.5, 0.5);
    for (std::size_t i = 0; i < ProfileEditor::kMostSteps + 20; ++i) {
        editor.moveSelection(0.0001, 0.0);
    }
    std::size_t steps = 0;
    while (editor.canUndo()) {
        editor.undo();
        ++steps;
    }
    CHECK(steps == ProfileEditor::kMostSteps - 1);
    CHECK(editor.profile().controls.size() == 1);
}

namespace {

QVariant formValue(const QVariantList& groups, const QString& key)
{
    for (const QVariant& group : groups) {
        for (const QVariant& item : group.toMap().value(QStringLiteral("fields")).toList()) {
            QVariantList cells = item.toMap().value(QStringLiteral("boxes")).toList();
            if (cells.isEmpty()) {
                cells.append(item);
            }
            for (const QVariant& cell : cells) {
                if (cell.toMap().value(QStringLiteral("key")).toString() == key) {
                    return cell.toMap().value(QStringLiteral("value"));
                }
            }
        }
    }
    return {};
}

std::filesystem::path freshFile(const char* name)
{
    const std::filesystem::path file = std::filesystem::temp_directory_path() / name;
    std::error_code ignored;
    std::filesystem::remove(file, ignored);
    return file;
}

}

TEST_CASE("profile editor: the form shows where a key sits, and typing there moves it like a drag")
{
    ProfileEditor editor(Profile{}, kScreen);
    const QString move = editor.addComponent(QStringLiteral("wasd"), 0.2, 0.7).value(QStringLiteral("id")).toString();
    const auto stickOf = [&] { return std::get<WasdControl>(editor.profile().controls.at(0).kind); };
    CHECK(formValue(editor.form(move), QStringLiteral("x")).toDouble() == doctest::Approx(20.0));
    CHECK(formValue(editor.form(move), QStringLiteral("y")).toDouble() == doctest::Approx(70.0));

    editor.placeControl(move, 0.5, 0.5);
    CHECK(formValue(editor.form(move), QStringLiteral("x")).toDouble() == doctest::Approx(50.0));
    CHECK(editor.setField(move, QStringLiteral("x"), 30.0));
    CHECK(centerOf(editor, 0).x == doctest::Approx(0.3));
    CHECK(centerOf(editor, 0).y == doctest::Approx(0.5));
    CHECK(editor.profile().controls[0].anchor == Anchor::Left);

    CHECK(editor.setField(move, QStringLiteral("radius"), 270.0));
    CHECK(stickOf().radius == doctest::Approx(0.25));
    CHECK(formValue(editor.form(move), QStringLiteral("radius")).toDouble() == doctest::Approx(270.0));
    CHECK(editor.setLength(move, QStringLiteral("radius"), 0.05));
    CHECK(formValue(editor.form(move), QStringLiteral("radius")).toDouble() == doctest::Approx(120.0));

    CHECK(editor.setField(move, QStringLiteral("anchor"), QStringLiteral("bottomRight")));
    CHECK(editor.profile().controls[0].anchor == Anchor::BottomRight);
    CHECK(centerOf(editor, 0).x == doctest::Approx(0.3));
    CHECK(centerOf(editor, 0).y == doctest::Approx(0.5));
    CHECK(formValue(editor.form(move), QStringLiteral("anchor")).toString() == QStringLiteral("bottomRight"));
    CHECK_FALSE(editor.setField(move, QStringLiteral("anchor"), QStringLiteral("middle")));
    CHECK_FALSE(editor.setField(move, QStringLiteral("nonsense"), 1.0));
}

TEST_CASE("profile editor: typing on in one box is one undo step, and typing back to the start leaves none")
{
    ProfileEditor editor(Profile{}, kScreen);
    const QString look = editor.addComponent(QStringLiteral("camera"), 0.7, 0.4).value(QStringLiteral("id")).toString();
    const auto cameraOf = [&] { return std::get<CameraControl>(editor.profile().controls.at(0).kind); };
    CHECK(editor.setField(look, QStringLiteral("idleRecenterMs"), 1.0));
    CHECK(editor.setField(look, QStringLiteral("idleRecenterMs"), 15.0));
    CHECK(editor.setField(look, QStringLiteral("idleRecenterMs"), 150.0));
    CHECK(cameraOf().idleRecenterMs == 150);
    editor.undo();
    CHECK(cameraOf().idleRecenterMs == 1000);
    editor.redo();
    CHECK(cameraOf().idleRecenterMs == 150);

    CHECK(editor.setField(look, QStringLiteral("sensitivityX"), 2.0));
    CHECK(editor.setField(look, QStringLiteral("idleRecenterMs"), 500.0));
    CHECK(editor.setField(look, QStringLiteral("sensitivityX"), 3.0));
    CHECK(cameraOf().sensitivity.x == doctest::Approx(3.0));
    editor.undo();
    CHECK(cameraOf().sensitivity.x == doctest::Approx(2.0));
    CHECK(cameraOf().idleRecenterMs == 500);
    editor.undo();
    CHECK(cameraOf().idleRecenterMs == 150);
    editor.undo();
    CHECK(cameraOf().sensitivity.x == doctest::Approx(1.0));

    CHECK(editor.setField(look, QStringLiteral("idleRecenterMs"), 2000.0));
    CHECK(editor.setField(look, QStringLiteral("idleRecenterMs"), 150.0));
    editor.undo();
    CHECK(cameraOf().idleRecenterMs == 1000);
    editor.undo();
    CHECK(editor.profile().controls.empty());
}

TEST_CASE("profile editor: the behavior page sets when the key works, from the switch variables")
{
    ProfileEditor editor(loaded(R"({"format":"gamescrcpy.profile","version":3,
        "vars": [{"id": "aiming", "name": "Aiming"}, {"id": "kills", "initial": 3}],
        "controls": [{"id": "fire", "type": "hold", "inputs": {"press": "KeyF"}}]})"),
                         kScreen);
    const QString fire = QStringLiteral("fire");
    CHECK_FALSE(formValue(editor.behavior(fire), QStringLiteral("trigger")).isValid());
    CHECK_FALSE(editor.setField(fire, QStringLiteral("trigger"), QStringLiteral("turbo")));

    QVariantMap when;
    for (const QVariant& field : editor.behavior(fire).at(0).toMap().value(QStringLiteral("fields")).toList()) {
        if (field.toMap().value(QStringLiteral("key")).toString() == QStringLiteral("when")) {
            when = field.toMap();
        }
    }
    const QVariantList options = when.value(QStringLiteral("options")).toList();
    REQUIRE(options.size() == 3);
    CHECK(options[0].toMap().value(QStringLiteral("text")).toString() == QStringLiteral("Always"));
    CHECK(options[1].toMap().value(QStringLiteral("value")).toString() == QStringLiteral("aiming"));
    CHECK(options[1].toMap().value(QStringLiteral("text")).toString() == QStringLiteral("When “Aiming” is on"));
    CHECK(options[2].toMap().value(QStringLiteral("value")).toString() == QStringLiteral("!aiming"));
    CHECK(editor.setField(fire, QStringLiteral("when"), QStringLiteral("!aiming")));
    CHECK(editor.profile().controls[0].when == "!aiming");
    CHECK(formValue(editor.behavior(fire), QStringLiteral("when")).toString() == QStringLiteral("!aiming"));
    CHECK(editor.setField(fire, QStringLiteral("when"), QString()));
    CHECK(editor.profile().controls[0].when.empty());
    CHECK_FALSE(editor.setField(fire, QStringLiteral("when"), QStringLiteral("nowhere")));
}

TEST_CASE("profile editor: a key saved as the default shapes new keys of its kind until the factory default is back")
{
    const std::filesystem::path file = freshFile("gs-component-defaults-test.json");
    gs::app::ComponentDefaults defaults(file);
    ProfileEditor editor(Profile{}, kScreen);
    const auto turboAt = [&](std::size_t index) { return std::get<TurboControl>(editor.profile().controls.at(index).kind); };

    const QString fire = editor.addComponent(QStringLiteral("turbo"), 0.8, 0.8).value(QStringLiteral("id")).toString();
    CHECK(editor.componentOf(fire) == QStringLiteral("turbo"));
    CHECK_FALSE(editor.hasSavedDefault(fire));
    CHECK(editor.bindInput(fire, QStringLiteral("inputs.press"), QStringLiteral("KeyR")));
    const QString bound = QString::fromStdString(editor.profile().controls[0].id);
    CHECK(bound == QStringLiteral("turboR"));
    CHECK(editor.setField(bound, QStringLiteral("rateHz"), 20.0));
    CHECK(editor.setField(bound, QStringLiteral("tapMs"), 40.0));
    CHECK_FALSE(editor.setField(bound, QStringLiteral("when"), QStringLiteral("!aiming")));
    CHECK(editor.saveAsDefault(bound));
    CHECK(editor.hasSavedDefault(bound));
    CHECK(std::filesystem::exists(file));

    const QVariantMap made = editor.addComponent(QStringLiteral("turbo"), 0.3, 0.3);
    CHECK(made.value(QStringLiteral("slot")).toString() == QStringLiteral("inputs.press"));
    CHECK(turboAt(1).rateHz == 20);
    CHECK(turboAt(1).tapMs == 40);
    CHECK_FALSE(turboAt(1).press.has_value());
    CHECK(centerOf(editor, 1).x == doctest::Approx(0.3));
    CHECK(editor.conflictingIds().isEmpty());

    const QString tap = editor.addComponent(QStringLiteral("tap"), 0.5, 0.5).value(QStringLiteral("id")).toString();
    CHECK_FALSE(editor.hasSavedDefault(tap));
    CHECK_FALSE(pressIn(editor, 2).has_value());

    {
        gs::app::ComponentDefaults reread(file);
        REQUIRE(reread.contains("turbo"));
        CHECK(reread.find("turbo")->value("type", std::string()) == "turbo");
        CHECK_FALSE(reread.find("turbo")->contains("id"));
        CHECK_FALSE(reread.find("turbo")->contains("offset"));
    }
    CHECK(editor.hasSavedDefault(fire));
    CHECK(editor.restoreFactoryDefault(fire));
    CHECK_FALSE(editor.hasSavedDefault(fire));
    CHECK_FALSE(gs::app::ComponentDefaults(file).contains("turbo"));
    editor.addComponent(QStringLiteral("turbo"), 0.6, 0.3);
    CHECK(turboAt(3).rateHz == 10);
    std::error_code ignored;
    std::filesystem::remove(file, ignored);
}

namespace {

void writeText(const std::filesystem::path& file, const std::string& text)
{
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string readText(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}

TEST_CASE("profile editor: a default saved before the components were split no longer fits and is left out (ADR-0091)")
{
    const std::filesystem::path file = freshFile("gs-component-defaults-old.json");
    writeText(file, R"({"format": "gamescrcpy.componentDefaults", "version": 1, "components": {
        "tap": {"type": "button", "tapMs": 40},
        "joystick": {"type": "joystick", "radius": 0.2}}})");
    gs::app::ComponentDefaults defaults(file);
    ProfileEditor editor(Profile{}, kScreen);
    editor.addComponent(QStringLiteral("tap"), 0.5, 0.5);
    CHECK(std::get<TapControl>(editor.profile().controls.at(0).kind).tapMs == 0);
    std::error_code ignored;
    std::filesystem::remove(file, ignored);
}

TEST_CASE("ADR-0112: profile editor: component defaults nested more than 64 levels deep are not read, so that they "
          "cannot overflow the stack")
{
    const std::filesystem::path file = freshFile("gs-component-defaults-deep.json");
    std::string deep;
    for (std::size_t level = 0; level < 30000; ++level) {
        deep += R"({"a":)";
    }
    deep += '1';
    deep += std::string(30000, '}');
    writeText(file, R"({"format": "gamescrcpy.componentDefaults", "version": 1, "components": {"deep":)" + deep + "}}");
    gs::app::ComponentDefaults defaults(file);
    CHECK_FALSE(defaults.contains("deep"));
    std::error_code ignored;
    std::filesystem::remove(file, ignored);
}

TEST_CASE("profile editor: a saved default's key goes on a new key when it is free, so no key press is asked for")
{
    const std::filesystem::path file = freshFile("gs-component-defaults-free.json");
    gs::app::ComponentDefaults defaults(file);
    ProfileEditor editor(Profile{}, kScreen);
    const QString first = editor.addComponent(QStringLiteral("tap"), 0.8, 0.8).value(QStringLiteral("id")).toString();
    CHECK(editor.bindInput(first, QStringLiteral("inputs.press"), QStringLiteral("KeyG")));
    const QString bound = QString::fromStdString(editor.profile().controls[0].id);
    CHECK(bound == QStringLiteral("tapG"));
    CHECK(editor.saveAsDefault(bound));
    CHECK(editor.rebind(bound, QStringLiteral("press"), QStringLiteral("KeyH")));

    const QVariantMap placed = editor.addComponent(QStringLiteral("tap"), 0.4, 0.4);
    CHECK(placed.value(QStringLiteral("slot")).toString().isEmpty());
    REQUIRE(pressIn(editor, 1).has_value());
    CHECK(pressIn(editor, 1)->input == *parseChord("KeyG"));
    const QVariantMap crowded = editor.addComponent(QStringLiteral("tap"), 0.3, 0.3);
    CHECK(crowded.value(QStringLiteral("slot")).toString() == QStringLiteral("inputs.press"));
    CHECK_FALSE(pressIn(editor, 2).has_value());
    CHECK(editor.conflictCount() == 0);
    editor.undo();
    editor.undo();
    CHECK(editor.profile().controls.size() == 1);
    std::error_code ignored;
    std::filesystem::remove(file, ignored);
}

TEST_CASE("profile editor: an unreadable defaults file is kept aside, and one from a newer version is left alone")
{
    const std::filesystem::path file = freshFile("gs-component-defaults-odd.json");
    std::filesystem::path aside = file;
    aside += ".bad";
    std::error_code ignored;
    std::filesystem::remove(aside, ignored);
    writeText(file, R"({"format": 5, "version": 1, "components": {"tap": {"type": "tap"}}})");
    {
        gs::app::ComponentDefaults defaults(file);
        CHECK_FALSE(defaults.contains("tap"));
        CHECK_FALSE(defaults.readOnly());
        CHECK(std::filesystem::exists(aside));
        CHECK(defaults.store("tap", nlohmann::json{{"type", "tap"}, {"tapMs", 40}}));
    }
    CHECK(gs::app::ComponentDefaults(file).find("tap").value_or(nlohmann::json::object()).value("tapMs", 0) == 40);

    const std::string newer =
        R"({"format": "gamescrcpy.componentDefaults", "version": 2, "components": {"tap": {"type": "tap"}}})";
    writeText(file, newer);
    {
        gs::app::ComponentDefaults defaults(file);
        CHECK(defaults.readOnly());
        CHECK_FALSE(defaults.contains("tap"));
        CHECK_FALSE(defaults.store("tap", nlohmann::json{{"type", "tap"}}));
    }
    CHECK(readText(file) == newer);
    std::filesystem::remove(file, ignored);
    std::filesystem::remove(aside, ignored);
}

TEST_CASE("profile editor: a default set while another program holds the file is kept, and written once it is free")
{
    const std::filesystem::path file = freshFile("gs-component-defaults-busy.json");
    const std::string before = R"({"format": "gamescrcpy.componentDefaults", "version": 1, "components": {}})";
    writeText(file, before);
    const auto hold = [&file] {
        return ::CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    };
    int busy = 0;
    {
        gs::app::ComponentDefaults defaults(file);
        ProfileEditor editor(Profile{}, kScreen);
        QObject::connect(&editor, &ProfileEditor::defaultsBusy, [&busy] { ++busy; });
        const QString fire =
            editor.addComponent(QStringLiteral("turbo"), 0.8, 0.8).value(QStringLiteral("id")).toString();
        CHECK(editor.bindInput(fire, QStringLiteral("inputs.press"), QStringLiteral("KeyR")));
        const QString bound = QString::fromStdString(editor.profile().controls[0].id);
        CHECK(bound == QStringLiteral("turboR"));
        CHECK(editor.setField(bound, QStringLiteral("rateHz"), 20.0));

        HANDLE reader = hold();
        REQUIRE(reader != INVALID_HANDLE_VALUE);
        CHECK(editor.saveAsDefault(bound));
        CHECK(busy == 1);
        CHECK(defaults.lastSaveBusy());
        CHECK(editor.hasSavedDefault(bound));
        editor.addComponent(QStringLiteral("turbo"), 0.3, 0.3);
        CHECK(std::get<TurboControl>(editor.profile().controls.at(1).kind).rateHz == 20);
        ::CloseHandle(reader);
        CHECK(readText(file) == before);

        CHECK(defaults.store("tap", nlohmann::json{{"type", "tap"}, {"tapMs", 40}}));
        CHECK_FALSE(defaults.lastSaveBusy());
        {
            gs::app::ComponentDefaults reread(file);
            CHECK(reread.contains("turbo"));
            CHECK(reread.contains("tap"));
        }

        reader = hold();
        REQUIRE(reader != INVALID_HANDLE_VALUE);
        CHECK(editor.restoreFactoryDefault(bound));
        CHECK(busy == 2);
        CHECK_FALSE(editor.hasSavedDefault(bound));
        ::CloseHandle(reader);
        CHECK(gs::app::ComponentDefaults(file).contains("turbo"));
    }
    CHECK(busy == 2);
    {
        gs::app::ComponentDefaults reread(file);
        CHECK_FALSE(reread.contains("turbo"));
        CHECK(reread.contains("tap"));
    }
    std::error_code ignored;
    std::filesystem::remove(file, ignored);
}

TEST_CASE("profile editor: a hand-edited default that does not fit is ignored and the factory one is used")
{
    const std::filesystem::path file = freshFile("gs-component-defaults-edited.json");
    writeText(file, R"({"format": "gamescrcpy.componentDefaults", "version": 1, "components": {
        "tap": {"type": 5},
        "turbo": {"type": "turbo", "rateHz": 99},
        "wasd": {"type": "wasd", "radius": 0.2, "inputs": {"up": "KeyI"}}}})");
    gs::app::ComponentDefaults defaults(file);
    ProfileEditor editor(Profile{}, kScreen);
    CHECK(editor.addComponent(QStringLiteral("tap"), 0.5, 0.5).value(QStringLiteral("slot")).toString() ==
          QStringLiteral("inputs.press"));
    CHECK_FALSE(pressIn(editor, 0).has_value());
    editor.addComponent(QStringLiteral("turbo"), 0.6, 0.5);
    CHECK(std::get<TurboControl>(editor.profile().controls.at(1).kind).rateHz == 10);
    editor.addComponent(QStringLiteral("wasd"), 0.2, 0.7);
    const auto& stick = std::get<WasdControl>(editor.profile().controls.at(2).kind);
    CHECK(stick.radius == doctest::Approx(0.2));
    CHECK(stick.up->input == *parseChord("KeyI"));
    CHECK_FALSE(stick.down.has_value());
    std::error_code ignored;
    std::filesystem::remove(file, ignored);
}

TEST_CASE("profile editor: a saved default whose mouse key is taken loses the claim with the key")
{
    const std::filesystem::path file = freshFile("gs-component-defaults-claim.json");
    gs::app::ComponentDefaults defaults(file);
    REQUIRE(defaults.store("longPress", nlohmann::json::parse(R"({"type": "longPress", "holdMs": 800,
        "inputs": {"press": {"input": "Mouse.Right", "claim": "cursor"}}})")));
    ProfileEditor editor(loaded(R"({"format":"gamescrcpy.profile","version":3,
        "controls": [{"id": "back", "type": "tap", "inputs": {"press": {"input": "Mouse.Right", "claim": "both"}}}]})"),
                         kScreen);
    const auto longOf = [&] { return std::get<LongPressControl>(editor.profile().controls.at(1).kind); };

    const QVariantMap placed = editor.addComponent(QStringLiteral("longPress"), 0.4, 0.4);
    const QString id = placed.value(QStringLiteral("id")).toString();
    CHECK(placed.value(QStringLiteral("slot")).toString() == QStringLiteral("inputs.press"));
    CHECK_FALSE(longOf().press.has_value());
    CHECK(longOf().holdMs == 800);
    CHECK_FALSE(editor.value(id, QStringLiteral("inputs.press.claim")).isValid());
    CHECK(editor.conflictingIds().isEmpty());

    CHECK(editor.bindInput(id, QStringLiteral("inputs.press"), QStringLiteral("KeyF")));
    CHECK(longOf().press->input == *parseChord("KeyF"));
    CHECK(longOf().holdMs == 800);
    std::error_code ignored;
    std::filesystem::remove(file, ignored);
}

TEST_CASE("profile editor: placing a key and recording it is one undo step, and each later rebinding is one more")
{
    ProfileEditor editor(Profile{}, kScreen);
    const auto pressOf = [&] { return *std::get<TurboControl>(editor.profile().controls.at(0).kind).press; };
    editor.beginGesture();
    const QVariantMap placed = editor.addComponent(QStringLiteral("turbo"), 0.5, 0.5);
    const QString id = placed.value(QStringLiteral("id")).toString();
    CHECK(editor.bindInput(id, placed.value(QStringLiteral("slot")).toString(), QStringLiteral("Mouse.X1")));
    editor.endGesture();
    const QString bound = QString::fromStdString(editor.profile().controls[0].id);
    CHECK(bound == QStringLiteral("turboMouseX1"));
    CHECK(editor.setValue(bound, QStringLiteral("inputs.press.claim"), QStringLiteral("both")));
    CHECK(editor.rebind(bound, QStringLiteral("press"), QStringLiteral("KeyT")));
    CHECK(pressOf().input == *parseChord("KeyT"));
    CHECK(pressOf().claim == Claim::Lock);

    editor.undo();
    CHECK(pressOf().input == *parseChord("Mouse.X1"));
    CHECK(pressOf().claim == Claim::Both);
    editor.undo();
    CHECK(pressOf().input == *parseChord("Mouse.X1"));
    CHECK(pressOf().claim == Claim::Lock);
    editor.undo();
    CHECK(editor.profile().controls.empty());
    CHECK_FALSE(editor.canUndo());
}

namespace {

constexpr const char* kThreeControls = R"({"format":"gamescrcpy.profile","version":3,"controls":[
    {"id":"fire","type":"turbo","inputs":{"press":"KeyQ"},"rateHz":12,"tapMs":40},
    {"id":"jump","type":"tap","inputs":{"press":"Space"}},
    {"id":"slide","type":"swipe","path":[[0,0],[0,0.1]],"inputs":{"press":"KeyE"}}]})";

}

TEST_CASE("profile editor: the condition form holds only the condition now that the action is the component itself (ADR-0091)")
{
    ProfileEditor editor(loaded(kThreeControls), kScreen);
    const QVariantList groups = editor.behavior(QStringLiteral("fire"));
    REQUIRE(groups.size() == 1);
    const QVariantList fields = groups[0].toMap().value(QStringLiteral("fields")).toList();
    REQUIRE(fields.size() == 1);
    CHECK(fields[0].toMap().value(QStringLiteral("key")) == QStringLiteral("when"));
    CHECK_FALSE(editor.setField(QStringLiteral("fire"), QStringLiteral("trigger"), QStringLiteral("tap")));
}

TEST_CASE("profile editor: the logic view opens only when the profile can have logic, and trying goes back to the picture (ADR-0091)")
{
    ProfileEditor editor(loaded(kThreeControls), kScreen);
    QStringList problems;
    QStringList wanted;
    QObject::connect(&editor, &ProfileEditor::logicProblem, [&problems](const QString& text) { problems.append(text); });
    QObject::connect(&editor, &ProfileEditor::logicWanted, [&wanted](const QString& folder) { wanted.append(folder); });

    int ensured = 0;
    editor.setLogic(QString(), QStringLiteral("Comes with an extension"), [&ensured](QString&) {
        ++ensured;
        return QString();
    });
    CHECK(editor.logicBlocked() == QStringLiteral("Comes with an extension"));
    CHECK_FALSE(editor.showLogic(true));
    CHECK(ensured == 0);
    CHECK(problems == QStringList{QStringLiteral("Comes with an extension")});
    CHECK_FALSE(editor.logicShown());

    editor.setLogic(QString(), QString(), [](QString& problem) {
        problem = QStringLiteral("Cannot open");
        return QString();
    });
    CHECK_FALSE(editor.showLogic(true));
    CHECK(problems.back() == QStringLiteral("Cannot open"));
    CHECK(wanted.isEmpty());

    editor.setLogic(QString(), QString(), [](QString&) { return QStringLiteral("C:/profiles/mine"); });
    CHECK_FALSE(editor.showLogic(true));
    CHECK(wanted == QStringList{QStringLiteral("C:/profiles/mine")});
    CHECK_FALSE(editor.logicShown());
    CHECK(editor.logic() == nullptr);

    auto* logic = new QObject;
    QObject::connect(&editor, &ProfileEditor::logicWanted, &editor,
                     [&editor, logic](const QString&) { editor.attachLogic(logic); });
    editor.setTrying(true);
    REQUIRE(editor.showLogic(true));
    CHECK(editor.logicShown());
    CHECK(editor.logic() == logic);
    CHECK(logic->parent() == &editor);
    CHECK_FALSE(editor.trying());

    editor.setTrying(true);
    CHECK(editor.trying());
    CHECK_FALSE(editor.logicShown());
    CHECK(editor.logic() == logic);

    REQUIRE(editor.showLogic(true));
    CHECK(wanted.size() == 2);
    CHECK_FALSE(editor.trying());
    CHECK(editor.showLogic(false));
    CHECK_FALSE(editor.logicShown());
    CHECK(editor.logic() == logic);
    CHECK_FALSE(editor.dirty());
}

namespace {

std::filesystem::path pathOf(const QString& text)
{
    return {text.toStdWString()};
}

QVariantMap rowWith(const QVariantList& rows, const QString& id)
{
    for (const QVariant& row : rows) {
        if (row.toMap().value(QStringLiteral("id")).toString() == id) {
            return row.toMap();
        }
    }
    return {};
}

}

TEST_CASE("profile editor: an example placed on the picture starts from its class, and the form follows static props (ADR-0091)")
{
    QTemporaryDir mine;
    REQUIRE(mine.isValid());
    gs::app::ComponentLibrary library(pathOf(QString::fromUtf8(GS_EXAMPLE_COMPONENTS_DIR)), pathOf(mine.path()));
    ProfileEditor editor(loaded(kThreeControls), kScreen);
    QStringList problems;
    QObject::connect(&editor, &ProfileEditor::logicProblem, [&problems](const QString& text) { problems.append(text); });

    editor.setLogic(QString(), QStringLiteral("Comes with an extension"), [](QString&) { return QString(); });
    CHECK(editor.addComponent(QStringLiteral("component:examples/in-turn"), 0.5, 0.5).isEmpty());
    CHECK(problems == QStringList{QStringLiteral("Comes with an extension")});
    CHECK(editor.profile().controls.size() == 3);

    editor.setLogic(QString(), QString(), [](QString&) { return QStringLiteral("C:/profiles/mine"); });
    CHECK(editor.addComponent(QStringLiteral("component:examples/none"), 0.5, 0.5).isEmpty());
    const QVariantMap added = editor.addComponent(QStringLiteral("component:examples/in-turn"), 0.5, 0.5);
    const QString id = added.value(QStringLiteral("id")).toString();
    CHECK(id == QStringLiteral("in-turn"));
    CHECK(added.value(QStringLiteral("slot")).toString().isEmpty());
    CHECK(editor.placedRef(id) == QStringLiteral("examples/in-turn"));
    CHECK(editor.placedRef(QStringLiteral("jump")).isEmpty());
    CHECK(editor.componentOf(id).isEmpty());
    REQUIRE(editor.profile().controls.size() == 4);
    const auto& placed = std::get<ComponentControl>(editor.profile().controls[3].kind);
    CHECK(placed.component == "examples/in-turn");
    REQUIRE(placed.keys.size() == 1);
    REQUIRE(placed.keys[0].binding.has_value());
    CHECK(placed.keys[0].binding->input == parseChord("KeyF"));
    REQUIRE(placed.props.size() == 2);
    CHECK(std::get<Vec2>(placed.props[0].value) == Vec2{0.12, 0});
    CHECK(std::get<Vec2>(placed.props[1].value) == Vec2{0.24, 0});

    const QVariantList groups = editor.form(id);
    REQUIRE_FALSE(groups.isEmpty());
    const QVariantList fields = groups[0].toMap().value(QStringLiteral("fields")).toList();
    REQUIRE(fields.size() == 3);
    CHECK(fields[0].toMap().value(QStringLiteral("key")).toString() == QStringLiteral("prop:key"));
    CHECK(fields[0].toMap().value(QStringLiteral("widget")).toString() == QStringLiteral("key"));
    CHECK(fields[0].toMap().value(QStringLiteral("title")).toString() == QStringLiteral("Key"));
    CHECK(fields[1].toMap().value(QStringLiteral("widget")).toString() == QStringLiteral("note"));
    CHECK(fields[2].toMap().value(QStringLiteral("key")).toString() == QStringLiteral("prop:third"));

    CHECK(editor.rebind(id, QStringLiteral("key"), QStringLiteral("KeyG")));
    CHECK(std::get<ComponentControl>(editor.profile().controls[3].kind).keys[0].binding->input == parseChord("KeyG"));
    CHECK_FALSE(editor.rebind(id, QStringLiteral("second"), QStringLiteral("KeyH")));

    const Vec2 center = centerOf(editor, 3);
    CHECK(editor.setPlacedPoint(id, QStringLiteral("second"), center.x + 0.1 * 1080.0 / 2400.0, center.y - 0.05));
    const Vec2 second = std::get<Vec2>(std::get<ComponentControl>(editor.profile().controls[3].kind).props[0].value);
    CHECK(second.x == doctest::Approx(0.1));
    CHECK(second.y == doctest::Approx(-0.05));
    CHECK_FALSE(editor.setPlacedPoint(id, QStringLiteral("key"), 0.5, 0.5));
    CHECK_FALSE(editor.setPlacedPoint(QStringLiteral("jump"), QStringLiteral("second"), 0.5, 0.5));

    const QVariantMap row = rowWith(editor.keyList(), id);
    CHECK(row.value(QStringLiteral("type")).toString() == QStringLiteral("component"));
    CHECK(row.value(QStringLiteral("component")).toString() == QStringLiteral("examples/in-turn"));
    const QVariantMap marker = rowWith(editor.markers(2400, 1080), id);
    CHECK(marker.value(QStringLiteral("icon")).toString() == QStringLiteral("refresh-cw"));
    CHECK_FALSE(marker.value(QStringLiteral("missing")).toBool());
    CHECK(marker.value(QStringLiteral("points")).toList().size() == 2);
}

TEST_CASE("INP-012: the example that shows all key hints while held is placed without a key and asks for one")
{
    QTemporaryDir mine;
    REQUIRE(mine.isValid());
    gs::app::ComponentLibrary library(pathOf(QString::fromUtf8(GS_EXAMPLE_COMPONENTS_DIR)), pathOf(mine.path()));
    ProfileEditor editor(loaded(kThreeControls), kScreen);
    editor.setLogic(QString(), QString(), [](QString&) { return QStringLiteral("C:/profiles/mine"); });
    const QVariantMap added = editor.addComponent(QStringLiteral("component:examples/show-all-hints"), 0.5, 0.5);
    const QString id = added.value(QStringLiteral("id")).toString();
    CHECK(id == QStringLiteral("show-all-hints"));
    CHECK(added.value(QStringLiteral("slot")).toString() == QStringLiteral("inputs.key"));
    REQUIRE(editor.profile().controls.size() == 4);
    CHECK(editor.profile().controls[3].name == "Show all key hints while held");
    const auto& placed = std::get<ComponentControl>(editor.profile().controls[3].kind);
    CHECK(placed.component == "examples/show-all-hints");
    REQUIRE(placed.keys.size() == 1);
    CHECK(placed.keys[0].name == "key");
    CHECK_FALSE(placed.keys[0].binding.has_value());
    CHECK(placed.props.empty());
    const QVariantMap marker = rowWith(editor.markers(2400, 1080), id);
    CHECK(marker.value(QStringLiteral("icon")).toString() == QStringLiteral("eye"));
    CHECK(marker.value(QStringLiteral("unbound")).toBool());
    CHECK(marker.value(QStringLiteral("points")).toList().isEmpty());

    CHECK(editor.rebind(id, QStringLiteral("key"), QStringLiteral("Tab")));
    const auto& bound = std::get<ComponentControl>(editor.profile().controls[3].kind);
    REQUIRE(bound.keys.size() == 1);
    REQUIRE(bound.keys[0].binding.has_value());
    CHECK(bound.keys[0].binding->input == parseChord("Tab"));
    CHECK_FALSE(rowWith(editor.markers(2400, 1080), id).value(QStringLiteral("unbound")).toBool());
}

TEST_CASE("profile editor: an instance's numbers, texts, switches and component picks change through the form and are checked (ADR-0091)")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const std::filesystem::path base = pathOf(root.path());
    gs::app::ComponentLibrary library(base / "examples", base / "mine");
    const QString ref = library.add(QStringLiteral("Helper"));
    REQUIRE(library.find(ref) != nullptr);
    writeText(pathOf(library.find(ref)->folder) / "component.ts", R"js(export default class Helper extends Component {
    static props = {
        times: prop.number(3, { min: 1, max: 9 }),
        word: prop.text("gg"),
        loop: prop.toggle(false),
        partner: prop.component(),
    };
}
)js");
    library.reload();
    ProfileEditor editor(loaded(kThreeControls), kScreen);
    editor.setLogic(QString(), QString(), [](QString&) { return QStringLiteral("C:/profiles/mine"); });
    const QVariantMap added = editor.addComponent(QStringLiteral("component:") + ref, 0.3, 0.3);
    const QString id = added.value(QStringLiteral("id")).toString();
    REQUIRE(id == QStringLiteral("helper"));
    CHECK(added.value(QStringLiteral("slot")).toString().isEmpty());

    CHECK(editor.setField(id, QStringLiteral("prop:times"), 20));
    CHECK(editor.setField(id, QStringLiteral("prop:word"), QStringLiteral("hello")));
    CHECK(editor.setField(id, QStringLiteral("prop:loop"), true));
    CHECK(editor.setField(id, QStringLiteral("prop:partner"), QStringLiteral("jump")));
    CHECK_FALSE(editor.setField(id, QStringLiteral("prop:partner"), QStringLiteral("nobody")));
    CHECK_FALSE(editor.setField(id, QStringLiteral("prop:partner"), id));
    CHECK_FALSE(editor.setField(id, QStringLiteral("prop:missing"), 1));
    const auto& placed = std::get<ComponentControl>(editor.profile().controls[3].kind);
    REQUIRE(placed.props.size() == 4);
    CHECK(std::get<double>(placed.props[0].value) == 9.0);
    CHECK(std::get<std::string>(placed.props[1].value) == "hello");
    CHECK(std::get<bool>(placed.props[2].value));
    CHECK(std::get<std::string>(placed.props[3].value) == "jump");

    const QVariantList fields = editor.form(id)[0].toMap().value(QStringLiteral("fields")).toList();
    REQUIRE(fields.size() == 4);
    CHECK(fields[0].toMap().value(QStringLiteral("value")).toDouble() == 9.0);
    CHECK(fields[0].toMap().value(QStringLiteral("maximum")).toDouble() == 9.0);
    CHECK(fields[1].toMap().value(QStringLiteral("widget")).toString() == QStringLiteral("text"));
    CHECK(fields[2].toMap().value(QStringLiteral("widget")).toString() == QStringLiteral("switch"));
    CHECK(fields[3].toMap().value(QStringLiteral("widget")).toString() == QStringLiteral("select"));
    CHECK(fields[3].toMap().value(QStringLiteral("options")).toList().size() == 4);
    CHECK(fields[3].toMap().value(QStringLiteral("value")).toString() == QStringLiteral("jump"));

    CHECK(editor.setField(id, QStringLiteral("prop:partner"), QString()));
    CHECK(std::get<ComponentControl>(editor.profile().controls[3].kind).props.size() == 3);
}

namespace {

QString classIn(gs::app::ComponentLibrary& library, const QString& name, const QString& base, const char* code)
{
    const QString ref = library.add(name, base);
    REQUIRE(library.find(ref) != nullptr);
    writeText(pathOf(library.find(ref)->folder) / "component.ts", code);
    library.reload();
    return ref;
}

const Control* controlWith(const ProfileEditor& editor, const std::string& id)
{
    const auto found = std::ranges::find(editor.profile().controls, id, &Control::id);
    return found != editor.profile().controls.end() ? &*found : nullptr;
}

}

TEST_CASE("profile editor: a component that extends a built-in one is placed as it, its class's values first in the form (ADR-0091)")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const std::filesystem::path base = pathOf(root.path());
    gs::app::ComponentLibrary library(base / "examples", base / "mine");
    const QString ref = classIn(library, QStringLiteral("Quick"), QStringLiteral("hold"), R"js(export default class Quick extends Hold {
    static props = { boost: prop.key("B"), times: prop.number(2), mark: prop.position(), target: prop.component(Tap) };

    onPressed() {}
}
)js");
    ProfileEditor editor(loaded(kThreeControls), kScreen);
    editor.setLogic(QString(), QString(), [](QString&) { return QStringLiteral("C:/profiles/mine"); });
    const QVariantMap added = editor.addComponent(QStringLiteral("component:") + ref, 0.4, 0.4);
    const QString id = added.value(QStringLiteral("id")).toString();
    REQUIRE(id == QStringLiteral("quick"));
    CHECK(added.value(QStringLiteral("slot")).toString() == QStringLiteral("inputs.press"));
    REQUIRE(editor.profile().controls.size() == 4);
    const Control& made = editor.profile().controls[3];
    CHECK(std::holds_alternative<HoldControl>(made.kind));
    CHECK(made.component == ref.toStdString());
    REQUIRE(made.keys.size() == 1);
    CHECK(made.keys[0].name == "boost");
    REQUIRE(made.keys[0].binding.has_value());
    CHECK(made.keys[0].binding->input == parseChord("KeyB"));
    REQUIRE(made.props.size() == 2);
    CHECK(std::get<double>(made.props[0].value) == 2.0);
    CHECK(std::get<Vec2>(made.props[1].value) == Vec2{0.12, 0});
    CHECK(editor.placedRef(id) == ref);
    CHECK(editor.componentOf(id).isEmpty());

    const QVariantList groups = editor.form(id);
    REQUIRE(groups.size() >= 2);
    const QVariantList own = groups[0].toMap().value(QStringLiteral("fields")).toList();
    REQUIRE(own.size() == 4);
    CHECK(own[0].toMap().value(QStringLiteral("key")).toString() == QStringLiteral("prop:boost"));
    CHECK(own[3].toMap().value(QStringLiteral("options")).toList().size() == 2);
    CHECK(std::ranges::any_of(groups.mid(1), [](const QVariant& group) {
        const QVariantList fields = group.toMap().value(QStringLiteral("fields")).toList();
        return std::ranges::any_of(fields, [](const QVariant& field) {
            return field.toMap().value(QStringLiteral("key")).toString() == QStringLiteral("key.press");
        });
    }));

    CHECK(editor.bindInput(id, QStringLiteral("inputs.press"), QStringLiteral("KeyH")));
    CHECK(pressIn(editor, 3)->input == parseChord("KeyH"));
    CHECK(editor.rebind(id, QStringLiteral("boost"), QStringLiteral("KeyN")));
    CHECK(editor.profile().controls[3].keys[0].binding->input == parseChord("KeyN"));
    CHECK(editor.rebind(id, QStringLiteral("press"), QStringLiteral("KeyJ")));
    CHECK(pressIn(editor, 3)->input == parseChord("KeyJ"));
    CHECK_FALSE(editor.rebind(id, QStringLiteral("nothing"), QStringLiteral("KeyM")));
    CHECK(editor.setField(id, QStringLiteral("prop:times"), 7));
    CHECK(std::get<double>(editor.profile().controls[3].props[0].value) == 7.0);
    CHECK(editor.setField(id, QStringLiteral("prop:target"), QStringLiteral("jump")));
    CHECK_FALSE(editor.setField(id, QStringLiteral("prop:target"), QStringLiteral("fire")));
    const Vec2 center = centerOf(editor, 3);
    CHECK(editor.setPlacedPoint(id, QStringLiteral("mark"), center.x + 0.1 * 1080.0 / 2400.0, center.y));
    CHECK(std::get<Vec2>(editor.profile().controls[3].props[1].value).x == doctest::Approx(0.1));

    const QVariantMap marker = rowWith(editor.markers(2400, 1080), id);
    CHECK(marker.value(QStringLiteral("type")).toString() == QStringLiteral("hold"));
    CHECK(marker.value(QStringLiteral("component")).toString() == ref);
    CHECK(marker.value(QStringLiteral("icon")).toString() == QStringLiteral("sparkles"));
    CHECK(marker.value(QStringLiteral("points")).toList().size() == 1);
    const QVariantMap row = rowWith(editor.keyList(), id);
    CHECK(row.value(QStringLiteral("type")).toString() == QStringLiteral("hold"));
    CHECK(row.value(QStringLiteral("component")).toString() == ref);
    const LoadResult again = loadProfile(saveProfile(editor.profile()));
    REQUIRE(again.ok());
    CHECK(again.profile == editor.profile());
}

TEST_CASE("profile editor: a group comes with its children, carries them when it moves and takes them along (ADR-0091)")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const std::filesystem::path base = pathOf(root.path());
    gs::app::ComponentLibrary library(base / "examples", base / "mine");
    const QString ref = classIn(library, QStringLiteral("Volley"), QString(), R"js(export default class Volley extends Component {
    static props = { key: prop.key("V") };
    cancel = this.child(Tap);
    skills = [this.child(Skill), this.child(Skill)];
}
)js");
    ProfileEditor editor(loaded(kThreeControls), kScreen);
    editor.setLogic(QString(), QString(), [](QString&) { return QStringLiteral("C:/profiles/mine"); });
    const QString id = editor.addComponent(QStringLiteral("component:") + ref, 0.5, 0.5).value(QStringLiteral("id")).toString();
    REQUIRE(id == QStringLiteral("volley"));
    REQUIRE(editor.profile().controls.size() == 7);
    const Control* cancel = controlWith(editor, "volley-cancel");
    REQUIRE(cancel != nullptr);
    CHECK(std::holds_alternative<TapControl>(cancel->kind));
    CHECK(cancel->parent == "volley");
    CHECK(cancel->child == "cancel");
    CHECK_FALSE(pressIn(editor, 4).has_value());
    REQUIRE(controlWith(editor, "volley-skills-1") != nullptr);
    CHECK(std::holds_alternative<SkillControl>(controlWith(editor, "volley-skills-1")->kind));
    CHECK(controlWith(editor, "volley-skills-0")->child == "skills.0");
    CHECK(rowWith(editor.markers(2400, 1080), QStringLiteral("volley-cancel")).value(QStringLiteral("parent")).toString() ==
          id);

    const QVariantList rows = editor.keyList();
    const auto at = std::ranges::find_if(rows, [&](const QVariant& row) {
        return row.toMap().value(QStringLiteral("id")).toString() == id;
    });
    REQUIRE(std::distance(at, rows.end()) >= 4);
    CHECK(at->toMap().value(QStringLiteral("depth")).toInt() == 0);
    for (int step = 1; step <= 3; ++step) {
        CHECK((at + step)->toMap().value(QStringLiteral("depth")).toInt() == 1);
        CHECK((at + step)->toMap().value(QStringLiteral("parent")).toString() == id);
    }

    editor.undo();
    CHECK(editor.profile().controls.size() == 3);
    editor.redo();
    REQUIRE(editor.profile().controls.size() == 7);

    const Vec2 group = centerOf(editor, 3);
    const Vec2 child = centerOf(editor, 4);
    editor.select({id});
    editor.moveSelection(0.05, 0.02);
    CHECK(centerOf(editor, 3).x == doctest::Approx(group.x + 0.05));
    CHECK(centerOf(editor, 4).x == doctest::Approx(child.x + 0.05));
    CHECK(centerOf(editor, 4).y == doctest::Approx(child.y + 0.02));
    editor.select({QStringLiteral("volley-cancel")});
    editor.moveSelection(0.0, 0.03);
    CHECK(centerOf(editor, 3).y == doctest::Approx(group.y + 0.02));
    CHECK(centerOf(editor, 4).y == doctest::Approx(child.y + 0.05));

    editor.select({id});
    const QStringList copies = editor.duplicateSelection();
    REQUIRE(copies == QStringList{QStringLiteral("volley2")});
    REQUIRE(editor.profile().controls.size() == 11);
    REQUIRE(controlWith(editor, "volley2-cancel") != nullptr);
    CHECK(controlWith(editor, "volley2-cancel")->parent == "volley2");
    CHECK(controlWith(editor, "volley2-skills-1")->child == "skills.1");
    editor.select({QStringLiteral("volley-cancel")});
    REQUIRE(editor.duplicateSelection().size() == 1);
    CHECK(editor.profile().controls.back().parent.empty());

    editor.select({id});
    editor.removeSelection();
    CHECK(editor.profile().controls.size() == 8);
    CHECK(std::ranges::none_of(editor.profile().controls, [](const Control& control) { return control.parent == "volley"; }));
    CHECK(controlWith(editor, "volley2-skills-0") != nullptr);
}

TEST_CASE("profile editor: opening a profile adds the children its group's class gained and lets the others go, in one step (ADR-0091)")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const std::filesystem::path base = pathOf(root.path());
    gs::app::ComponentLibrary library(base / "examples", base / "mine");
    const QString ref = classIn(library, QStringLiteral("Volley"), QString(), R"js(export default class Volley extends Component {
    cancel = this.child(Tap);
    skills = [this.child(Skill), this.child(Skill)];
}
)js");
    REQUIRE(ref == QStringLiteral("mine/volley"));
    const Profile original = loaded(R"({"format":"gamescrcpy.profile","version":3,"controls":[
        {"id":"volley","type":"component","component":"mine/volley"},
        {"id":"volley-cancel","type":"tap","parent":"volley","child":"cancel"},
        {"id":"old","type":"hold","parent":"volley","child":"gone"},
        {"id":"wrong","type":"hold","parent":"volley","child":"skills.1"},
        {"id":"lost","type":"tap","parent":"nobody","child":"cancel"}]})");
    ProfileEditor editor(original, kScreen);
    CHECK(editor.canUndo());
    CHECK(editor.dirty());
    REQUIRE(editor.profile().controls.size() == 7);
    CHECK(controlWith(editor, "volley-cancel")->parent == "volley");
    CHECK(controlWith(editor, "old")->parent.empty());
    CHECK(controlWith(editor, "old")->child.empty());
    CHECK(controlWith(editor, "wrong")->parent.empty());
    CHECK(controlWith(editor, "lost")->parent.empty());
    REQUIRE(controlWith(editor, "volley-skills-0") != nullptr);
    CHECK(controlWith(editor, "volley-skills-1")->child == "skills.1");
    CHECK(std::holds_alternative<SkillControl>(controlWith(editor, "volley-skills-1")->kind));
    editor.undo();
    CHECK(editor.profile() == original);
    CHECK_FALSE(editor.dirty());
    CHECK_FALSE(editor.canUndo());

    ProfileEditor plain(loaded(kThreeControls), kScreen);
    CHECK_FALSE(plain.canUndo());
}

TEST_CASE("profile editor: a draft preview cannot be edited and marks added, changed and removed keys")
{
    const Profile live = loaded(R"({"format":"gamescrcpy.profile","version":3,"controls":[
        {"id":"jump","name":"Jump","type":"hold","anchor":"bottomRight","offset":[-0.2,-0.3],"inputs":{"press":"Space"}},
        {"id":"aim","name":"Aim","type":"hold","anchor":"topRight","offset":[-0.1,0.2],"inputs":{"press":"Mouse.Right"}}]})");
    const Profile drafted = loaded(R"({"format":"gamescrcpy.profile","version":3,"controls":[
        {"id":"jump","name":"Jump!","type":"hold","anchor":"bottomRight","offset":[-0.2,-0.3],"inputs":{"press":"Space"}},
        {"id":"fire","name":"Fire","type":"tap","anchor":"bottomLeft","offset":[0.3,-0.2],"inputs":{"press":"Mouse.Left"}}]})");
    ProfileEditor editor(drafted, kScreen);
    CHECK_FALSE(editor.preview());
    CHECK(editor.previewClient().isEmpty());

    editor.setPreview({.client = QStringLiteral("Claude"), .target = QStringLiteral("profile:draft"),
                       .live = live, .liveFolder = QString(), .draftFolder = QString()});
    CHECK(editor.preview());
    CHECK(editor.previewClient() == QStringLiteral("Claude"));
    CHECK(editor.previewTarget() == QStringLiteral("profile:draft"));

    static_cast<void>(editor.addControl(QStringLiteral("tap"), 0.5, 0.5));
    editor.moveSelection(0.1, 0.1);
    CHECK(editor.profile() == drafted);
    editor.undo();
    CHECK(editor.profile() == drafted);
    editor.setTrying(true);
    CHECK_FALSE(editor.trying());

    const QVariantList markers = editor.markers(2400, 1080);
    const QVariantMap jump = rowWith(markers, QStringLiteral("jump"));
    const QVariantMap fire = rowWith(markers, QStringLiteral("fire"));
    const QVariantMap aim = rowWith(markers, QStringLiteral("aim"));
    REQUIRE_FALSE(jump.isEmpty());
    REQUIRE_FALSE(fire.isEmpty());
    REQUIRE_FALSE(aim.isEmpty());
    CHECK(jump.value(QStringLiteral("diff")).toString() == QStringLiteral("changed"));
    CHECK(fire.value(QStringLiteral("diff")).toString() == QStringLiteral("added"));
    CHECK(aim.value(QStringLiteral("diff")).toString() == QStringLiteral("removed"));
    CHECK(aim.value(QStringLiteral("ghost")).toBool());
    CHECK(aim.value(QStringLiteral("name")).toString() == QStringLiteral("Aim"));
    CHECK(aim.value(QStringLiteral("x")).toDouble() > 0);
    CHECK(aim.value(QStringLiteral("y")).toDouble() > 0);
}

TEST_CASE("profile editor: the preview reads the live file for the code diff and refuses paths outside the folder")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const std::filesystem::path live = pathOf(root.path()) / "live";
    const std::filesystem::path draft = pathOf(root.path()) / "draft";
    std::filesystem::create_directories(live);
    std::filesystem::create_directories(draft);
    writeText(live / "logic.ts", "line 1\nline 2\n");
    writeText(draft / "logic.ts", "line 1\nline 2 changed\n");
    ProfileEditor editor(loaded(R"({"format":"gamescrcpy.profile","version":3,"controls":[]})"), kScreen);
    editor.setPreview({.client = QStringLiteral("Claude"), .target = QStringLiteral("profile:draft"),
                       .live = Profile{}, .liveFolder = root.path() + QStringLiteral("/live"),
                       .draftFolder = root.path() + QStringLiteral("/draft")});
    CHECK(editor.liveTextOf(QStringLiteral("logic.ts")) == QStringLiteral("line 1\nline 2\n"));
    CHECK(editor.liveTextOf(QStringLiteral("notes.md")).isEmpty());
    CHECK(editor.liveTextOf(QStringLiteral("../draft/logic.ts")).isEmpty());
    const QVariantList diff = editor.codeDiff(QStringLiteral("logic.ts"));
    REQUIRE_FALSE(diff.isEmpty());
    CHECK(diff.front().toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("header"));
    CHECK(diff.back().toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("added"));
    CHECK(diff.back().toMap().value(QStringLiteral("text")).toString() == QStringLiteral("line 2 changed"));
    CHECK(editor.codeDiff(QStringLiteral("notes.md")).isEmpty());
    CHECK(editor.codeDiff(QString()).isEmpty());
}

TEST_CASE("profile editor: a component is named after the key it is bound to, numbered when that name is taken")
{
    ProfileEditor editor(Profile{}, kScreen);
    const QString first = editor.addComponent(QStringLiteral("hold"), 0.5, 0.5).value(QStringLiteral("id")).toString();
    CHECK(first == QStringLiteral("hold"));
    CHECK(editor.setKey(first, QStringLiteral("inputs.press"), QStringLiteral("KeyF")));
    CHECK(idAt(editor, 0) == QStringLiteral("holdF"));

    const QString second = editor.addComponent(QStringLiteral("hold"), 0.6, 0.5).value(QStringLiteral("id")).toString();
    CHECK(second == QStringLiteral("hold"));
    CHECK(editor.setKey(second, QStringLiteral("inputs.press"), QStringLiteral("KeyF")));
    CHECK(idAt(editor, 1) == QStringLiteral("holdF2"));

    const QString third = editor.addComponent(QStringLiteral("tap"), 0.7, 0.5).value(QStringLiteral("id")).toString();
    CHECK(editor.setKey(third, QStringLiteral("inputs.press"), QStringLiteral("Space")));
    CHECK(idAt(editor, 2) == QStringLiteral("tapSpace"));

    const QString fourth = editor.addComponent(QStringLiteral("hold"), 0.8, 0.5).value(QStringLiteral("id")).toString();
    CHECK(editor.setKey(fourth, QStringLiteral("inputs.press"), QStringLiteral("Mouse.Left")));
    CHECK(idAt(editor, 3) == QStringLiteral("holdMouseLeft"));

    const QString fifth = editor.addComponent(QStringLiteral("hold"), 0.9, 0.5).value(QStringLiteral("id")).toString();
    CHECK(editor.setKey(fifth, QStringLiteral("inputs.press"), QStringLiteral("Ctrl+Shift+KeyG")));
    CHECK(idAt(editor, 4) == QStringLiteral("holdCtrlShiftG"));

    const QString sixth = editor.addComponent(QStringLiteral("hold"), 0.4, 0.5).value(QStringLiteral("id")).toString();
    CHECK(editor.setKey(sixth, QStringLiteral("inputs.press"), QStringLiteral("Digit0")));
    CHECK(idAt(editor, 5) == QStringLiteral("hold0"));
}

TEST_CASE("profile editor: changing a key renames the component's uses in the logic, and one undo restores the file")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString folder = dir.path();
    const QByteArray before = "controls.hold.onPress(async () => {\n"
                              "  controls[\"hold\"].press();\n"
                              "  log.info(\"hold\");\n"
                              "});\n";
    writeAll(folder, QStringLiteral("logic.ts"), before);

    ProfileEditor editor(Profile{}, kScreen);
    editor.setLogic(folder, QString(), [](QString&) { return QString(); });
    const QString id = editor.addComponent(QStringLiteral("hold"), 0.5, 0.5).value(QStringLiteral("id")).toString();
    CHECK(id == QStringLiteral("hold"));
    CHECK(editor.setKey(id, QStringLiteral("inputs.press"), QStringLiteral("KeyF")));
    CHECK(idAt(editor, 0) == QStringLiteral("holdF"));
    CHECK(readAll(folder, QStringLiteral("logic.ts")) ==
          QByteArray("controls.holdF.onPress(async () => {\n"
                     "  controls[\"holdF\"].press();\n"
                     "  log.info(\"hold\");\n"
                     "});\n"));

    editor.undo();
    CHECK(idAt(editor, 0) == QStringLiteral("hold"));
    CHECK(readAll(folder, QStringLiteral("logic.ts")) == before);
    CHECK(editor.selection() == QStringList{QStringLiteral("hold")});

    editor.redo();
    CHECK(idAt(editor, 0) == QStringLiteral("holdF"));
    CHECK(readAll(folder, QStringLiteral("logic.ts")).contains("controls.holdF.onPress"));
}

TEST_CASE("profile editor: a component's own file follows its code name, and undo and redo move it back and forth "
          "(ADR-0091)")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString folder = dir.path();
    writeAll(folder, QStringLiteral("logic.ts"), "log.info(\"start\");\n");
    REQUIRE(QDir(folder).mkpath(QStringLiteral("controls")));
    const QByteArray own = "controls.hold.onPress(() => {\n  log.info(\"own\");\n}, { consume: false });\n";
    writeAll(folder, QStringLiteral("controls/hold.ts"), own);
    writeAll(folder, QStringLiteral("controls/hold.blueprint.json"), "{}\n");

    ProfileEditor editor(Profile{}, kScreen);
    editor.setLogic(folder, QString(), [](QString&) { return QString(); });
    const QString id = editor.addComponent(QStringLiteral("hold"), 0.5, 0.5).value(QStringLiteral("id")).toString();
    CHECK(id == QStringLiteral("hold"));
    CHECK(editor.setKey(id, QStringLiteral("inputs.press"), QStringLiteral("KeyF")));
    CHECK(idAt(editor, 0) == QStringLiteral("holdF"));
    CHECK_FALSE(QFile::exists(QDir(folder).filePath(QStringLiteral("controls/hold.ts"))));
    CHECK(readAll(folder, QStringLiteral("controls/holdF.ts")) ==
          QByteArray("controls.holdF.onPress(() => {\n  log.info(\"own\");\n}, { consume: false });\n"));
    CHECK(readAll(folder, QStringLiteral("controls/holdF.blueprint.json")) == QByteArray("{}\n"));

    editor.undo();
    CHECK(idAt(editor, 0) == QStringLiteral("hold"));
    CHECK(readAll(folder, QStringLiteral("controls/hold.ts")) == own);
    CHECK_FALSE(QFile::exists(QDir(folder).filePath(QStringLiteral("controls/holdF.ts"))));
    CHECK(QFile::exists(QDir(folder).filePath(QStringLiteral("controls/hold.blueprint.json"))));

    editor.redo();
    CHECK(QFile::exists(QDir(folder).filePath(QStringLiteral("controls/holdF.ts"))));
    CHECK_FALSE(QFile::exists(QDir(folder).filePath(QStringLiteral("controls/hold.ts"))));
}

TEST_CASE("profile editor: a code name set by hand stops following the key, and the name is checked")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString folder = dir.path();
    writeAll(folder, QStringLiteral("logic.ts"), QByteArray("controls.shoot.press();\n"));

    ProfileEditor editor(Profile{}, kScreen);
    editor.setLogic(folder, QString(), [](QString&) { return QString(); });
    const QString id = editor.addComponent(QStringLiteral("hold"), 0.5, 0.5).value(QStringLiteral("id")).toString();
    CHECK(editor.setKey(id, QStringLiteral("inputs.press"), QStringLiteral("KeyF")));
    CHECK(idAt(editor, 0) == QStringLiteral("holdF"));
    const QString other = editor.addComponent(QStringLiteral("tap"), 0.6, 0.5).value(QStringLiteral("id")).toString();
    CHECK(other == QStringLiteral("tap"));

    CHECK_FALSE(editor.codeNameProblem(idAt(editor, 0), QStringLiteral("")).isEmpty());
    CHECK_FALSE(editor.codeNameProblem(idAt(editor, 0), QStringLiteral("9start")).isEmpty());
    CHECK_FALSE(editor.codeNameProblem(idAt(editor, 0), QStringLiteral("has space")).isEmpty());
    CHECK_FALSE(editor.codeNameProblem(idAt(editor, 0), other).isEmpty());
    CHECK_FALSE(editor.codeNameProblem(idAt(editor, 0), QStringLiteral("class")).isEmpty());
    CHECK(editor.codeNameProblem(idAt(editor, 0), QStringLiteral("shoot")).isEmpty());
    CHECK_FALSE(editor.renameCode(idAt(editor, 0), QStringLiteral("has space")));
    CHECK(editor.renameCode(idAt(editor, 0), QStringLiteral("shoot")));

    REQUIRE(editor.profile().controls.size() == 2);
    CHECK(editor.profile().controls[0].id == "shoot");
    CHECK(editor.profile().controls[0].idFixed);
    CHECK(readAll(folder, QStringLiteral("logic.ts")) == QByteArray("controls.shoot.press();\n"));

    CHECK(editor.setKey(QStringLiteral("shoot"), QStringLiteral("inputs.press"), QStringLiteral("KeyG")));
    CHECK(editor.profile().controls[0].id == "shoot");

    CHECK(editor.renameCode(QStringLiteral("shoot"), QStringLiteral("shoot")) == false);
    CHECK(editor.renameCode(QStringLiteral("shoot"), QStringLiteral("fire")));
    CHECK(readAll(folder, QStringLiteral("logic.ts")) == QByteArray("controls.fire.press();\n"));
    editor.undo();
    CHECK(editor.profile().controls[0].id == "shoot");
    CHECK(readAll(folder, QStringLiteral("logic.ts")) == QByteArray("controls.shoot.press();\n"));
}

TEST_CASE("profile editor: deleting a component the logic uses asks first, and both answers do what they say")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString folder = dir.path();
    const QByteArray before = "controls.hold.onPress(async () => {\n"
                              "  await sleep(10);\n"
                              "});\n"
                              "keys.F.onPress(async () => {\n"
                              "  controls.hold.press();\n"
                              "});\n";
    writeAll(folder, QStringLiteral("logic.ts"), before);
    REQUIRE(QDir(folder).mkpath(QStringLiteral("controls")));
    const QByteArray own = "controls.hold.onPress(() => {\n}, { consume: false });\n";
    writeAll(folder, QStringLiteral("controls/hold.ts"), own);
    writeAll(folder, QStringLiteral("controls/hold.blueprint.json"), "{}\n");
    const auto ownFileThere = [&folder](const char* name) {
        return QFile::exists(QDir(folder).filePath(QString::fromUtf8(name)));
    };

    ProfileEditor editor(Profile{}, kScreen);
    editor.setLogic(folder, QString(), [](QString&) { return QString(); });
    const QString id = editor.addComponent(QStringLiteral("hold"), 0.5, 0.5).value(QStringLiteral("id")).toString();
    int asked = 0;
    QObject::connect(&editor, &ProfileEditor::deleteAsked, [&asked](int uses) { asked = uses; });

    editor.select({id});
    editor.removeSelection();
    CHECK(asked == 3);
    CHECK(editor.profile().controls.size() == 1);
    CHECK(readAll(folder, QStringLiteral("logic.ts")) == before);

    editor.deleteSelection(false);
    CHECK(editor.profile().controls.empty());
    CHECK(readAll(folder, QStringLiteral("logic.ts")) == before);
    CHECK(readAll(folder, QStringLiteral("controls/hold.ts")) == own);
    editor.undo();
    CHECK(editor.profile().controls.size() == 1);
    editor.redo();
    CHECK(editor.profile().controls.empty());

    editor.undo();
    REQUIRE(editor.profile().controls.size() == 1);
    editor.select({QString::fromStdString(editor.profile().controls[0].id)});
    editor.deleteSelection(true);
    CHECK(editor.profile().controls.empty());
    CHECK(readAll(folder, QStringLiteral("logic.ts")) ==
          QByteArray("keys.F.onPress(async () => {\n"
                     "});\n"));
    CHECK_FALSE(ownFileThere("controls/hold.ts"));
    CHECK_FALSE(ownFileThere("controls/hold.blueprint.json"));
    editor.undo();
    CHECK(editor.profile().controls.size() == 1);
    CHECK(readAll(folder, QStringLiteral("logic.ts")) == before);
    CHECK(readAll(folder, QStringLiteral("controls/hold.ts")) == own);
    CHECK(readAll(folder, QStringLiteral("controls/hold.blueprint.json")) == QByteArray("{}\n"));
}
