#include "KeyHintModel.h"
#include "KeyLabels.h"

#include <doctest/doctest.h>

#include <QVariantMap>

#include <string>
#include <string_view>

using gs::app::keyHintItems;
using gs::app::keyLabel;

namespace {

gs::input::Chord chord(std::string_view text)
{
    const auto parsed = gs::input::parseChord(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

gs::input::Profile load(std::string_view body)
{
    const gs::input::LoadResult result =
        gs::input::loadProfile(R"({"format":"gamescrcpy.profile","version":3,)" + std::string(body) + "}");
    REQUIRE(result.ok());
    return result.profile;
}

}

TEST_CASE("key labels follow the keyboard layout, shorten the named keys and mark the mouse (INP-013)")
{
    const gs::app::KeyLabel letter = keyLabel(chord("KeyW"));
    CHECK(letter.text.size() == 1);
    CHECK(letter.text == letter.text.toUpper());
    CHECK_FALSE(letter.mouse);
    CHECK(keyLabel(chord("Space")).text == QStringLiteral("Space"));
    CHECK(keyLabel(chord("Escape")).text == QStringLiteral("Esc"));
    CHECK(keyLabel(chord("F5")).text == QStringLiteral("F5"));
    CHECK(keyLabel(chord("Tab")).text == QStringLiteral("Tab"));
    CHECK(keyLabel(chord("ArrowUp")).text == QStringLiteral("\u2191"));
    CHECK(keyLabel(chord("Numpad5")).text == QStringLiteral("Num5"));
    CHECK(keyLabel(chord("NumpadAdd")).text == QStringLiteral("Num+"));
    CHECK(keyLabel(chord("ShiftLeft")).text == QStringLiteral("Shift"));
    CHECK(keyLabel(chord("Ctrl+Shift+F1")).text == QStringLiteral("Ctrl+Shift+F1"));

    const gs::app::KeyLabel left = keyLabel(chord("Mouse.Left"));
    CHECK(left.text == QStringLiteral("LMB"));
    CHECK(left.mouse);
    CHECK(keyLabel(chord("Mouse.X1")).text == QStringLiteral("M4"));
    CHECK(keyLabel(chord("Mouse.WheelDown")).text == QStringLiteral("Wheel \u2193"));
    CHECK(keyLabel(chord("Shift+Mouse.Right")).text == QStringLiteral("Shift+RMB"));
    CHECK(keyLabel(chord("Shift+Mouse.Right")).glyph == QStringLiteral("right"));
    CHECK(keyLabel(chord("Shift+Mouse.Right")).prefix == QStringLiteral("Shift+"));
    CHECK(keyLabel(chord("Mouse.WheelUp")).glyph == QStringLiteral("wheelUp"));
    CHECK(keyLabel(chord("Mouse.X2")).glyph == QStringLiteral("forward"));
    CHECK(keyLabel(chord("Mouse.Move")).glyph.isEmpty());
    CHECK(keyLabel(chord("KeyF")).glyph.isEmpty());
    CHECK(keyLabel(chord("Gamepad.LeftShoulder")).text == QStringLiteral("LB"));
    CHECK(keyLabel(chord("Gamepad.A")).text == QStringLiteral("A"));
    CHECK_FALSE(keyLabel(chord("Gamepad.A")).mouse);
}

TEST_CASE("key hint items place each control on the picture with its labels and marks (INP-020)")
{
    const gs::input::Profile profile = load(R"("controls":[
        {"id":"fire","type":"turbo","inputs":{"press":"Mouse.Left"}},
        {"id":"move","type":"wasd","anchor":"bottomLeft","offset":[0.3,-0.3],"radius":0.1,
         "inputs":{"up":"KeyW","down":"KeyS"}},
        {"id":"horn","enabled":false,"type":"tap","anchor":"topRight","inputs":{"press":"Escape"}}])");
    const QVariantList items = keyHintItems(profile, QSizeF(800, 400), chord("Backquote"));
    REQUIRE(items.size() == 3);

    const QVariantMap fire = items[0].toMap();
    CHECK(fire[QStringLiteral("control")].toInt() == 0);
    CHECK(fire[QStringLiteral("type")].toString() == QStringLiteral("turbo"));
    CHECK(fire[QStringLiteral("x")].toDouble() == doctest::Approx(400));
    CHECK(fire[QStringLiteral("y")].toDouble() == doctest::Approx(200));
    CHECK(fire[QStringLiteral("live")].toBool());
    const QVariantList fireKeys = fire[QStringLiteral("keys")].toList();
    REQUIRE(fireKeys.size() == 1);
    CHECK(fireKeys[0].toMap()[QStringLiteral("text")].toString() == QStringLiteral("LMB"));
    CHECK(fireKeys[0].toMap()[QStringLiteral("glyph")].toString() == QStringLiteral("left"));
    CHECK(fireKeys[0].toMap()[QStringLiteral("mouse")].toBool());
    CHECK_FALSE(fireKeys[0].toMap().contains(QStringLiteral("tag")));

    const QVariantMap move = items[1].toMap();
    CHECK(move[QStringLiteral("x")].toDouble() == doctest::Approx(120));
    CHECK(move[QStringLiteral("y")].toDouble() == doctest::Approx(280));
    CHECK(move[QStringLiteral("rx")].toDouble() == doctest::Approx(40));
    CHECK(move[QStringLiteral("ry")].toDouble() == doctest::Approx(40));
    const QVariantList moveKeys = move[QStringLiteral("keys")].toList();
    REQUIRE(moveKeys.size() == 2);
    CHECK(moveKeys[1].toMap()[QStringLiteral("slot")].toString() == QStringLiteral("down"));
    CHECK(move[QStringLiteral("type")].toString() == QStringLiteral("wasd"));

    const QVariantMap horn = items[2].toMap();
    CHECK(horn[QStringLiteral("control")].toInt() == 2);
    CHECK(horn[QStringLiteral("x")].toDouble() == doctest::Approx(800));
    CHECK(horn[QStringLiteral("y")].toDouble() == doctest::Approx(0));
    CHECK_FALSE(horn[QStringLiteral("live")].toBool());

    CHECK(keyHintItems(profile, QSizeF(), chord("Backquote")).isEmpty());
}

TEST_CASE("the crosshair is wanted while a live camera asks for it (ADR-0077)")
{
    const gs::input::Profile profile = load(R"("controls":[
        {"id":"fire","type":"hold","inputs":{"press":"Mouse.Left"}},
        {"id":"look","type":"camera","inputs":{"look":"Mouse.Move"},"crosshair":true},
        {"id":"spare","type":"camera","enabled":false,"inputs":{"look":"Mouse.Move"},"crosshair":true}])");
    CHECK(gs::app::crosshairWanted(profile, {}));
    CHECK(gs::app::crosshairWanted(profile, {true, true, false}));
    CHECK_FALSE(gs::app::crosshairWanted(profile, {true, false, false}));
    CHECK(gs::app::crosshairWanted(profile, {true, false, true}));

    const gs::input::Profile plain = load(R"("controls":[{"id":"look","type":"camera","inputs":{"look":"Mouse.Move"}}])");
    CHECK_FALSE(gs::app::crosshairWanted(plain, {}));
    CHECK_FALSE(gs::app::crosshairWanted(plain, {true}));

    const gs::input::Profile two = load(R"("controls":[
        {"id":"walk","type":"camera","inputs":{"look":"Mouse.Move"}},
        {"id":"scope","type":"camera","inputs":{"look":"Mouse.Move"},"crosshair":true}])");
    CHECK_FALSE(gs::app::crosshairWanted(two, {true, true}));
    CHECK(gs::app::crosshairWanted(two, {false, true}));

    const gs::input::Profile gated = load(R"("vars":[{"id":"aiming"}],"controls":[
        {"id":"scope","type":"camera","when":"aiming","inputs":{"look":"Mouse.Move"},"crosshair":true}])");
    CHECK_FALSE(gs::app::crosshairWanted(gated, {}));
    CHECK(gs::app::crosshairWanted(gated, {true}));
}
