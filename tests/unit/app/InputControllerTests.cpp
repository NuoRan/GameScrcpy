#include "ControlChannel.h"
#include "InputController.h"

#include <doctest/doctest.h>

#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>
#include <vector>

using gs::app::ControlChannel;
using gs::app::InputController;

TEST_CASE("key mapping engages with a profile and focus, text input pauses it and ends with another device (INP-009)")
{
    constexpr std::uintptr_t kNeverInFront = 1;
    InputController controller;
    std::vector<bool> changes;
    QObject::connect(&controller, &InputController::textInputChanged, [&changes](bool on) { changes.push_back(on); });
    const auto channel = std::make_shared<ControlChannel>();
    controller.setTarget(QStringLiteral("A"), channel, kNeverInFront);
    controller.setFocused(true);
    CHECK(controller.keymap());
    CHECK_FALSE(controller.engaged());

    controller.setTextInput(true);
    CHECK_FALSE(controller.textInput());

    controller.setProfile(std::make_shared<const gs::input::Profile>());
    CHECK(controller.engaged());
    controller.setTextInput(true);
    CHECK(controller.textInput());
    CHECK_FALSE(controller.engaged());
    controller.setTextInput(false);
    CHECK(controller.engaged());

    controller.setTextInput(true);
    controller.setTarget(QStringLiteral("A"), channel, kNeverInFront);
    CHECK(controller.textInput());
    controller.setTarget(QStringLiteral("B"), channel, kNeverInFront);
    CHECK_FALSE(controller.textInput());
    CHECK(controller.engaged());

    controller.setTextInput(true);
    controller.setKeymap(false);
    CHECK_FALSE(controller.textInput());
    CHECK_FALSE(controller.engaged());
    CHECK(changes == std::vector<bool>{true, false, true, false, true, false});
}

TEST_CASE("the controller knows the keys a profile binds, including the default lock key (ADR-0070)")
{
    InputController controller;
    const gs::input::LoadResult loaded = gs::input::loadProfile(
        R"({"format":"gamescrcpy.profile","version":3,
            "controls":[{"id":"fire","type":"hold","inputs":{"press":"KeyF"}},
                        {"id":"shot","type":"tap","inputs":{"press":"Ctrl+KeyP"}}]})");
    REQUIRE(loaded.ok());
    controller.setProfile(std::make_shared<const gs::input::Profile>(loaded.profile));
    CHECK(controller.binds(*gs::input::parseSource("KeyF")));
    CHECK(controller.binds(*gs::input::parseSource("KeyP")));
    CHECK(controller.binds(*gs::input::parseSource("Backquote")));
    CHECK_FALSE(controller.binds(*gs::input::parseSource("F11")));
    controller.setProfile(nullptr);
    CHECK_FALSE(controller.binds(*gs::input::parseSource("KeyF")));
}
