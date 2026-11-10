#include "ProfileEditor.h"
#include "ProfileForm.h"

#include <doctest/doctest.h>

#include <QVariantMap>

#include <set>
#include <string>
#include <utility>
#include <variant>

using gs::app::ProfileEditor;
using gs::app::ProfileForm;
using namespace gs::input;
namespace form = gs::app::form;

namespace {

const ScreenGeometry kScreen{.width = 2400, .height = 1080};

Profile loaded(const char* json)
{
    LoadResult result = loadProfile(json);
    REQUIRE(result.ok());
    return result.profile;
}

template <typename Variant, typename Check>
void eachKind(Check&& check)
{
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (check(Variant{std::in_place_index<I>}, I), ...);
    }(std::make_index_sequence<std::variant_size_v<Variant>>{});
}

template <typename Kind, typename Entry>
void checkCoverage(const Kind& kind, const Entry& entry)
{
    std::set<std::string> declared;
    for (const form::Declared& value : form::declaredOf(entry)) {
        CHECK_MESSAGE(declared.insert(value.key).second, value.key);
    }
    std::set<std::string> listed;
    std::set<std::string> keys;
    for (const form::Field& field : form::fieldsOf(kind)) {
        listed.insert(std::string(field.path));
        CHECK_MESSAGE(keys.insert(std::string(field.key)).second, field.key);
        CHECK_MESSAGE(declared.contains(std::string(field.path)), field.path);
    }
    std::set<std::string> excluded;
    for (const form::Excluded& out : form::excludedOf(kind)) {
        CHECK_MESSAGE(excluded.insert(out.path).second, out.path);
        CHECK_MESSAGE(!listed.contains(out.path), out.path);
        CHECK_MESSAGE(declared.contains(out.path), out.path);
    }
    for (const std::string& key : declared) {
        CHECK_MESSAGE((listed.contains(key) || excluded.contains(key)), key);
    }
}

QVariantMap fieldOf(const QVariantList& groups, const QString& key)
{
    for (const QVariant& group : groups) {
        for (const QVariant& item : group.toMap().value(QStringLiteral("fields")).toList()) {
            QVariantList cells = item.toMap().value(QStringLiteral("boxes")).toList();
            if (cells.isEmpty()) {
                cells.append(item);
            }
            for (const QVariant& cell : cells) {
                if (cell.toMap().value(QStringLiteral("key")).toString() == key) {
                    return cell.toMap();
                }
            }
        }
    }
    return {};
}

}

TEST_CASE("profile form: every declared field of every kind is in the table or set aside with a reason")
{
    eachKind<ControlKind>([](const ControlKind& kind, std::size_t index) {
        CAPTURE(index);
        Control control;
        control.kind = kind;
        checkCoverage(kind, control);
    });
}

TEST_CASE("profile form: a slot bound to a mouse button offers when it applies, starting from the slot's default")
{
    ProfileEditor editor(loaded(R"({"format":"gamescrcpy.profile","version":3,
        "controls": [
            {"id": "fire", "type": "hold", "inputs": {"press": "KeyF"}},
            {"id": "move", "type": "rightWalk", "inputs": {"walkTo": "Mouse.Right"}},
            {"id": "keys", "type": "wasd", "inputs": {"up": "KeyW"}}
        ]})"),
                         kScreen);
    QString fire = QStringLiteral("fire");
    const QString move = QStringLiteral("move");
    const auto pressOf = [&] { return *std::get<HoldControl>(editor.profile().controls.at(0).kind).press; };
    const auto walkOf = [&] { return *std::get<RightWalkControl>(editor.profile().controls.at(1).kind).walkTo; };

    CHECK(fieldOf(editor.form(fire), QStringLiteral("claim.press")).isEmpty());
    CHECK(editor.rebind(fire, QStringLiteral("press"), QStringLiteral("Mouse.Left")));
    fire = QString::fromStdString(editor.profile().controls.at(0).id);
    CHECK(fire == QStringLiteral("holdMouseLeft"));
    QVariantMap claim = fieldOf(editor.form(fire), QStringLiteral("claim.press"));
    REQUIRE_FALSE(claim.isEmpty());
    CHECK(claim.value(QStringLiteral("widget")).toString() == QStringLiteral("select"));
    CHECK(claim.value(QStringLiteral("title")).toString() == QStringLiteral("When it applies"));
    CHECK(claim.value(QStringLiteral("hint")).toString() == QStringLiteral("Left button"));
    CHECK(claim.value(QStringLiteral("value")).toString() == QStringLiteral("lock"));
    const QVariantList options = claim.value(QStringLiteral("options")).toList();
    REQUIRE(options.size() == 3);
    CHECK(options[0].toMap().value(QStringLiteral("text")).toString() == QStringLiteral("While the view is locked"));
    CHECK(options[1].toMap().value(QStringLiteral("text")).toString() == QStringLiteral("In cursor mode"));
    CHECK(options[2].toMap().value(QStringLiteral("value")).toString() == QStringLiteral("both"));

    CHECK(editor.setField(fire, QStringLiteral("claim.press"), QStringLiteral("both")));
    CHECK(pressOf().claim == Claim::Both);
    CHECK(editor.value(fire, QStringLiteral("inputs.press.claim")).toString() == QStringLiteral("both"));
    CHECK(editor.setField(fire, QStringLiteral("claim.press"), QStringLiteral("lock")));
    CHECK(pressOf().claim == Claim::Lock);
    CHECK_FALSE(editor.value(fire, QStringLiteral("inputs.press.claim")).isValid());
    CHECK_FALSE(editor.setField(fire, QStringLiteral("claim.press"), QStringLiteral("sideways")));

    CHECK(fieldOf(editor.form(QStringLiteral("keys")), QStringLiteral("claim.up")).isEmpty());
    CHECK(fieldOf(editor.form(move), QStringLiteral("claim.walkTo")).value(QStringLiteral("value")).toString() ==
          QStringLiteral("cursor"));
    CHECK(editor.setField(move, QStringLiteral("claim.walkTo"), QStringLiteral("lock")));
    CHECK(walkOf().claim == Claim::Lock);
    CHECK(editor.value(move, QStringLiteral("inputs.walkTo.claim")).toString() == QStringLiteral("lock"));
    CHECK(editor.setField(move, QStringLiteral("claim.walkTo"), QStringLiteral("cursor")));
    CHECK(walkOf().claim == Claim::Cursor);
    CHECK_FALSE(editor.value(move, QStringLiteral("inputs.walkTo.claim")).isValid());

    CHECK(editor.setField(fire, QStringLiteral("claim.press"), QStringLiteral("cursor")));
    CHECK(editor.setKey(fire, QStringLiteral("inputs.press"), QStringLiteral("KeyQ")));
    CHECK(pressOf().input == *parseChord("KeyQ"));
    CHECK(pressOf().claim == Claim::Lock);
}

TEST_CASE("profile form: clearing a key drops the binding, and the component stays without one")
{
    ProfileEditor editor(loaded(R"({"format":"gamescrcpy.profile","version":3,
        "controls": [
            {"id": "fire", "type": "tap", "inputs": {"press": {"input": "Mouse.Right", "claim": "both"}}},
            {"id": "skill", "type": "skill", "direction": "cursor",
             "inputs": {"press": "KeyQ", "cancel": "Space", "cast": "Mouse.Left"}}
        ]})"),
                         kScreen);
    const QString fire = QStringLiteral("fire");
    const QString skill = QStringLiteral("skill");
    const auto aim = [&] { return std::get<SkillControl>(editor.profile().controls.at(1).kind); };

    const QVariantMap cast = fieldOf(editor.form(skill), QStringLiteral("castKey"));
    CHECK(cast.value(QStringLiteral("title")).toString() == QStringLiteral("Cast key"));
    CHECK(cast.value(QStringLiteral("widget")).toString() == QStringLiteral("key"));
    CHECK(cast.value(QStringLiteral("bindPath")).toString() == QStringLiteral("inputs.cast"));
    CHECK(cast.value(QStringLiteral("clearPath")).toString() == QStringLiteral("inputs.cast"));
    REQUIRE(cast.value(QStringLiteral("keys")).toList().size() == 1);
    CHECK(fieldOf(editor.form(skill), QStringLiteral("claim.cast")).value(QStringLiteral("value")).toString() ==
          QStringLiteral("cursor"));
    CHECK(fieldOf(editor.form(skill), QStringLiteral("claim.cancel")).isEmpty());

    CHECK(editor.clearKey(fire, QStringLiteral("inputs.press")));
    CHECK_FALSE(std::get<TapControl>(editor.profile().controls.at(0).kind).press.has_value());
    CHECK_FALSE(editor.value(fire, QStringLiteral("inputs.press")).isValid());

    CHECK(editor.clearKey(skill, QStringLiteral("inputs.cancel")));
    CHECK_FALSE(aim().cancel.has_value());
    CHECK(editor.clearKey(skill, QStringLiteral("inputs.cast")));
    CHECK_FALSE(aim().castKey.has_value());
    editor.undo();
    REQUIRE(aim().castKey.has_value());
    CHECK(aim().castKey->input == *parseChord("Mouse.Left"));
    CHECK(editor.setKey(skill, QStringLiteral("inputs.cast"), QStringLiteral("KeyE")));
    CHECK(aim().castKey->input == *parseChord("KeyE"));
    CHECK(aim().castKey->claim == Claim::Cursor);
    CHECK(editor.setKey(skill, QStringLiteral("inputs.cancel"), QStringLiteral("Mouse.X2")));
    CHECK(aim().cancel->input == *parseChord("Mouse.X2"));
    CHECK_FALSE(fieldOf(editor.form(skill), QStringLiteral("claim.cancel")).isEmpty());
    CHECK(loadProfile(saveProfile(editor.profile())).profile == editor.profile());
}

TEST_CASE("profile form: the camera's crosshair is a switch")
{
    ProfileEditor editor(Profile{}, kScreen);
    const QString look =
        editor.addComponent(QStringLiteral("camera"), 0.7, 0.4).value(QStringLiteral("id")).toString();
    const QVariantMap crosshair = fieldOf(editor.form(look), QStringLiteral("crosshair"));
    CHECK(crosshair.value(QStringLiteral("widget")).toString() == QStringLiteral("switch"));
    CHECK(crosshair.value(QStringLiteral("title")).toString() == QStringLiteral("Crosshair"));
    CHECK_FALSE(crosshair.value(QStringLiteral("value")).toBool());
    CHECK(editor.setField(look, QStringLiteral("crosshair"), true));
    CHECK(std::get<CameraControl>(editor.profile().controls.at(0).kind).crosshair);
    CHECK(fieldOf(editor.form(look), QStringLiteral("crosshair")).value(QStringLiteral("value")).toBool());
    CHECK(editor.setField(look, QStringLiteral("crosshair"), false));
    CHECK_FALSE(editor.value(look, QStringLiteral("crosshair")).isValid());
}

TEST_CASE("profile form: each kind of button shows its own timing, and the rate and hold time are set in place")
{
    ProfileEditor editor(Profile{}, kScreen);
    const auto add = [&](const char* kind) {
        return editor.addComponent(QString::fromLatin1(kind), 0.5, 0.5).value(QStringLiteral("id")).toString();
    };
    const QString tap = add("tap");
    const QString hold = add("hold");
    const QString longPress = add("longPress");
    const QString turbo = add("turbo");
    CHECK_FALSE(fieldOf(editor.form(tap), QStringLiteral("tapMs")).isEmpty());
    CHECK(fieldOf(editor.form(hold), QStringLiteral("tapMs")).isEmpty());
    CHECK(fieldOf(editor.form(hold), QStringLiteral("holdMs")).isEmpty());
    const QVariantMap holdFor = fieldOf(editor.form(longPress), QStringLiteral("holdMs"));
    CHECK(holdFor.value(QStringLiteral("widget")).toString() == QStringLiteral("slider"));
    CHECK(holdFor.value(QStringLiteral("value")).toDouble() == doctest::Approx(1000.0));
    const QVariantMap rate = fieldOf(editor.form(turbo), QStringLiteral("rateHz"));
    CHECK(rate.value(QStringLiteral("value")).toDouble() == doctest::Approx(10.0));
    CHECK_FALSE(fieldOf(editor.form(turbo), QStringLiteral("tapMs")).isEmpty());

    CHECK(editor.setField(longPress, QStringLiteral("holdMs"), 1500.0));
    CHECK(std::get<LongPressControl>(editor.profile().controls.at(2).kind).holdMs == 1500);
    CHECK(editor.setField(turbo, QStringLiteral("rateHz"), 15.0));
    CHECK(std::get<TurboControl>(editor.profile().controls.at(3).kind).rateHz == 15);
    CHECK_FALSE(editor.setField(turbo, QStringLiteral("rateHz"), 99.0));
}

TEST_CASE("profile form: right-click walk shows its walking speed, release behaviour and hero position")
{
    ProfileEditor editor(Profile{}, kScreen);
    const QString walk =
        editor.addComponent(QStringLiteral("rightWalk"), 0.3, 0.7).value(QStringLiteral("id")).toString();
    CHECK_FALSE(fieldOf(editor.form(walk), QStringLiteral("walkMsPerUnit")).isEmpty());
    CHECK_FALSE(fieldOf(editor.form(walk), QStringLiteral("stopOnRelease")).isEmpty());
    CHECK(fieldOf(editor.form(walk), QStringLiteral("heroX")).value(QStringLiteral("value")).toDouble() ==
          doctest::Approx(50.0));
    CHECK(editor.setField(walk, QStringLiteral("heroX"), 60.0));
    CHECK(std::get<RightWalkControl>(editor.profile().controls.at(0).kind).hero.x > 0.0);
}

TEST_CASE("profile form: the list model updates rows in place and resets only when the rows change")
{
    ProfileEditor editor(loaded(R"({"format":"gamescrcpy.profile","version":3,
        "controls": [{"id": "move", "type": "wasd", "inputs": {"up": "KeyW"}}]})"),
                         kScreen);
    const QString move = QStringLiteral("move");
    ProfileForm model;
    int resets = 0;
    int changes = 0;
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++resets; });
    QObject::connect(&model, &QAbstractItemModel::dataChanged, [&] { ++changes; });
    model.setEditor(&editor);
    CHECK(model.rowCount() == 0);
    model.setKeyId(move);
    const int rows = model.rowCount();
    CHECK(rows > 0);
    CHECK(model.data(model.index(0), ProfileForm::FirstRole).toBool());
    CHECK_FALSE(model.data(model.index(0), ProfileForm::SeparatedRole).toBool());
    CHECK(model.data(model.index(rows - 1), ProfileForm::LastRole).toBool());

    resets = 0;
    CHECK(editor.setField(move, QStringLiteral("radius"), 200.0));
    CHECK(resets == 0);
    CHECK(changes == 1);
    CHECK(model.rowCount() == rows);

    CHECK(editor.rebind(move, QStringLiteral("left"), QStringLiteral("Mouse.X1")));
    CHECK(resets == 1);
    CHECK(model.rowCount() > rows);

    model.setBehavior(true);
    CHECK(resets == 2);
    CHECK(model.rowCount() == 1);
    const QVariantList cells = model.data(model.index(0), ProfileForm::FieldsRole).toList();
    REQUIRE(cells.size() == 1);
    CHECK(cells[0].toMap().value(QStringLiteral("key")).toString() == QStringLiteral("when"));

    model.setKeyId(QStringLiteral("missing"));
    CHECK(model.rowCount() == 0);
}

TEST_CASE("profile form: a skill lists its key, then cancel and the cast key, and the cast key's claim sits with the timing")
{
    ProfileEditor editor(Profile{}, kScreen);
    const QString skill =
        editor.addComponent(QStringLiteral("skill"), 0.6, 0.6).value(QStringLiteral("id")).toString();
    const QVariantList groups = editor.form(skill);
    const auto keysOf = [&](qsizetype group) {
        QStringList keys;
        for (const QVariant& field : groups.at(group).toMap().value(QStringLiteral("fields")).toList()) {
            keys.append(field.toMap().value(QStringLiteral("key")).toString());
        }
        return keys;
    };
    REQUIRE(groups.size() == 4);
    CHECK(keysOf(0) == QStringList{QStringLiteral("key.press"), QStringLiteral("cancel"), QStringLiteral("castKey")});
    CHECK(keysOf(1) == QStringList{QStringLiteral("x"), QStringLiteral("anchor")});
    CHECK(keysOf(2) == QStringList{QStringLiteral("cast"), QStringLiteral("claim.cast")});
    CHECK(keysOf(3) == QStringList{QStringLiteral("castRadius"), QStringLiteral("heroX"), QStringLiteral("pauses")});
}

TEST_CASE("profile form: the view's size is shown and typed in percent of the picture")
{
    ProfileEditor editor(Profile{}, kScreen);
    const QString look =
        editor.addComponent(QStringLiteral("camera"), 0.7, 0.4).value(QStringLiteral("id")).toString();
    const auto sizeOf = [&] { return std::get<CameraControl>(editor.profile().controls.at(0).kind).size; };
    const auto shown = [&](const char* key) {
        return fieldOf(editor.form(look), QString::fromLatin1(key)).value(QStringLiteral("value")).toDouble();
    };
    CHECK(shown("width") == doctest::Approx(36.0));
    CHECK(shown("height") == doctest::Approx(60.0));
    CHECK(editor.setField(look, QStringLiteral("width"), 50.0));
    CHECK(sizeOf().x == doctest::Approx(0.5 * 2400 / 1080));
    CHECK(sizeOf().y == doctest::Approx(0.6));
    CHECK(shown("width") == doctest::Approx(50.0));
    CHECK(editor.setArea(look, QStringLiteral("size"), 0.25, 0.5));
    CHECK(shown("width") == doctest::Approx(25.0));
    CHECK(shown("height") == doctest::Approx(50.0));
    editor.undo();
    editor.undo();
    CHECK(sizeOf().x == doctest::Approx(0.8));
}
