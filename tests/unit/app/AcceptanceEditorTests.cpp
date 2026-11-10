#include "ProfileEditor.h"

#include <doctest/doctest.h>

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <variant>
#include <vector>

using gs::app::ProfileEditor;
using namespace gs::input;

namespace {

const ScreenGeometry kWide{.width = 2400, .height = 1080};

Profile startingProfile()
{
    const LoadResult loaded = loadProfile(R"({"format":"gamescrcpy.profile","version":3,
        "vars":[{"id":"aiming","name":"Aiming"}],
        "controls":[]})");
    REQUIRE(loaded.ok());
    return loaded.profile;
}

class History
{
public:
    explicit History(ProfileEditor& editor) : m_editor(editor) {}

    template <typename Edit>
    void step(const char* name, const Edit& edit)
    {
        CAPTURE(name);
        const Profile before = m_editor.profile();
        edit();
        const Profile after = m_editor.profile();
        CHECK_FALSE(after == before);
        REQUIRE(m_editor.canUndo());
        m_editor.undo();
        CHECK(m_editor.profile() == before);
        REQUIRE(m_editor.canRedo());
        m_editor.redo();
        CHECK(m_editor.profile() == after);
        CHECK_FALSE(m_editor.canRedo());
    }

private:
    ProfileEditor& m_editor;
};

QString idOf(const QVariantMap& added)
{
    return added.value(QStringLiteral("id")).toString();
}

}

TEST_CASE("EDT-005: undo and redo cover every editing operation, each one step")
{
    ProfileEditor editor(startingProfile(), kWide);
    const Profile initial = editor.profile();
    History history(editor);

    QString tap;
    QString move;
    QString skill;
    QString look;
    QString swipe;
    QString multi;
    QString wheel;
    QStringList copies;
    history.step("add a control", [&] { tap = editor.addControl(QStringLiteral("tap"), 0.8, 0.8); });
    history.step("add a component", [&] { move = idOf(editor.addComponent(QStringLiteral("wasd"), 0.2, 0.7)); });
    history.step("bind a key", [&] {
        CHECK(editor.bindInput(tap, QStringLiteral("inputs.press"), QStringLiteral("KeyF")));
        tap = QString::fromStdString(editor.profile().controls.at(0).id);
        CHECK(tap == QStringLiteral("tapF"));
    });
    history.step("rebind a key", [&] {
        CHECK(editor.rebind(tap, QStringLiteral("press"), QStringLiteral("KeyG")));
        tap = QString::fromStdString(editor.profile().controls.at(0).id);
        CHECK(tap == QStringLiteral("tapG"));
    });
    history.step("rename", [&] { CHECK(editor.rename(tap, QStringLiteral("Fire"))); });
    history.step("set a value", [&] { CHECK(editor.setValue(move, QStringLiteral("radius"), 0.2)); });
    history.step("type a position into the form", [&] { CHECK(editor.setField(move, QStringLiteral("x"), 30.0)); });
    history.step("add a skill", [&] { skill = idOf(editor.addComponent(QStringLiteral("skill"), 0.6, 0.6)); });
    history.step("set a key by its path", [&] {
        CHECK(editor.setKey(skill, QStringLiteral("inputs.cancel"), QStringLiteral("Space")));
    });
    history.step("clear a key", [&] { CHECK(editor.clearKey(skill, QStringLiteral("inputs.cancel"))); });
    history.step("choose when the key works", [&] { CHECK(editor.setWhen(tap, QStringLiteral("aiming"))); });
    history.step("move the selection", [&] {
        editor.select({tap});
        editor.moveSelection(0.05, 0.0);
    });
    history.step("drag in one gesture", [&] {
        editor.select({move});
        editor.beginGesture();
        for (int i = 0; i < 5; ++i) {
            editor.moveSelection(0.01, 0.01);
        }
        editor.endGesture();
    });
    history.step("place a control", [&] { editor.placeControl(skill, 0.5, 0.4); });
    history.step("drag a range", [&] { CHECK(editor.setLength(move, QStringLiteral("radius"), 0.1)); });
    history.step("add a camera", [&] { look = editor.addControl(QStringLiteral("camera"), 0.7, 0.4); });
    history.step("drag an area", [&] { CHECK(editor.setArea(look, QStringLiteral("size"), 0.5, 0.4)); });
    history.step("drag a point", [&] { CHECK(editor.setPoint(skill, QStringLiteral("hero"), 0.6, 0.5)); });
    history.step("add a swipe", [&] { swipe = editor.addControl(QStringLiteral("swipe"), 0.5, 0.5); });
    history.step("drag a path point", [&] { CHECK(editor.setPathPoint(swipe, 1, 0.5, 0.25)); });
    history.step("add a multi-press", [&] { multi = idOf(editor.addComponent(QStringLiteral("multiPress"), 0.4, 0.6)); });
    history.step("add places", [&] { CHECK(editor.setField(multi, QStringLiteral("places"), 3.0)); });
    history.step("drag a place", [&] { CHECK(editor.setPlacedPoint(multi, QStringLiteral("points.1"), 0.45, 0.3)); });
    history.step("add an action wheel", [&] {
        wheel = idOf(editor.addComponent(QStringLiteral("actionWheel"), 0.8, 0.3));
    });
    history.step("change the cell count", [&] { CHECK(editor.setWheelCellCount(wheel, 4)); });
    history.step("name a cell", [&] {
        CHECK(editor.setWheelCell(wheel, 0, QStringLiteral("name"), QStringLiteral("potion")));
    });
    history.step("make a cell tap", [&] {
        CHECK(editor.setWheelCell(wheel, 1, QStringLiteral("action"), QStringLiteral("tap")));
    });
    history.step("place a cell's tap", [&] { CHECK(editor.setPlacedPoint(wheel, QStringLiteral("cells.1"), 0.9, 0.8)); });
    history.step("align keys", [&] {
        editor.select({tap, skill, look});
        editor.alignSelection(QStringLiteral("top"));
    });
    history.step("spread keys", [&] {
        editor.select({tap, skill, look});
        editor.distributeSelection(true);
    });
    history.step("duplicate", [&] {
        editor.select({tap});
        copies = editor.duplicateSelection();
    });
    REQUIRE(copies.size() == 1);
    history.step("remove", [&] {
        editor.select(copies);
        editor.removeSelection();
    });
    history.step("start closed", [&] { CHECK(editor.setValue(look, QStringLiteral("enabled"), false)); });
    history.step("hand the gamepad to the game", [&] { CHECK(editor.setGamepadPassthrough(true)); });
    history.step("accept a suggested key", [&] {
        Control suggested;
        suggested.kind = HoldControl{};
        const QStringList ids =
            editor.suggest({ProfileEditor::Suggested{.control = suggested, .at = Vec2{0.3, 0.3}}});
        REQUIRE(ids.size() == 1);
        CHECK(editor.acceptSuggestion(ids[0]));
    });

    const Profile finished = editor.profile();
    CHECK(loadProfile(saveProfile(finished)).ok());
    while (editor.canUndo()) {
        editor.undo();
    }
    CHECK(editor.profile() == initial);
    CHECK_FALSE(editor.dirty());
    while (editor.canRedo()) {
        editor.redo();
    }
    CHECK(editor.profile() == finished);
    CHECK(editor.dirty());
}

TEST_CASE("CTL-008: a multi-press holds 2 to 10 places, each dragged on the picture, and its marker shows every place")
{
    ProfileEditor editor(Profile{}, kWide);
    const QVariantMap added = editor.addComponent(QStringLiteral("multiPress"), 0.5, 0.5);
    QString id = idOf(added);
    CHECK(added.value(QStringLiteral("slot")).toString() == QStringLiteral("inputs.press"));
    CHECK(id == QStringLiteral("multi"));
    CHECK(editor.bindInput(id, QStringLiteral("inputs.press"), QStringLiteral("KeyQ")));
    id = QString::fromStdString(editor.profile().controls.at(0).id);
    CHECK(id == QStringLiteral("multiQ"));
    const auto pointsOf = [&] { return std::get<MultiPressControl>(editor.profile().controls.at(0).kind).points; };
    const auto placesOf = [&] {
        return editor.markers(2400, 1080).at(0).toMap().value(QStringLiteral("points")).toList();
    };
    CHECK(pointsOf().size() == 1);
    CHECK(placesOf().size() == 1);

    CHECK(editor.setField(id, QStringLiteral("places"), 4.0));
    CHECK(pointsOf().size() == 3);
    CHECK(placesOf().size() == 3);
    CHECK(editor.setField(id, QStringLiteral("places"), 40.0));
    CHECK(pointsOf().size() == kMostMultiPressPoints);
    CHECK(editor.setField(id, QStringLiteral("places"), 1.0));
    CHECK(pointsOf().size() == 1);
    CHECK(editor.setField(id, QStringLiteral("places"), 3.0));
    REQUIRE(pointsOf().size() == 2);

    CHECK(editor.setPlacedPoint(id, QStringLiteral("points.1"), 0.75, 0.25));
    const QVariantList places = placesOf();
    REQUIRE(places.size() == 2);
    const QVariantMap place = places.at(1).toMap();
    CHECK(place.value(QStringLiteral("name")).toString() == QStringLiteral("points.1"));
    CHECK(place.value(QStringLiteral("x")).toDouble() == doctest::Approx(0.75 * 2400).epsilon(0.001));
    CHECK(place.value(QStringLiteral("y")).toDouble() == doctest::Approx(0.25 * 1080).epsilon(0.001));
    CHECK_FALSE(editor.setPlacedPoint(id, QStringLiteral("points.2"), 0.5, 0.5));
    CHECK(loadProfile(saveProfile(editor.profile())).profile == editor.profile());

    editor.undo();
    CHECK(placesOf().at(1).toMap().value(QStringLiteral("x")).toDouble() != doctest::Approx(0.75 * 2400));
}
