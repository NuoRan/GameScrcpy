#include "CompanionInput.h"
#include "ControlChannel.h"
#include "InputController.h"
#include "Settings.h"

#include <gs/protocol/Enums.h>

#include <doctest/doctest.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include <cstdint>
#include <memory>
#include <string>

using gs::app::InputController;
using gs::app::PhoneInputBox;
using gs::app::TypingChange;
namespace protocol = gs::protocol;

namespace {

constexpr std::uintptr_t kNeverInFront = 1;

protocol::InputBoxChanged box(bool shown, std::uint32_t inputType = 1,
                              protocol::ImeAction action = protocol::ImeAction::Unspecified, bool multiLine = false)
{
    return protocol::InputBoxChanged{.shown = shown, .multiLine = multiLine, .inputType = inputType, .action = action};
}

QVariantMap key(const QString& slot, const QString& text)
{
    return {{QStringLiteral("slot"), slot}, {QStringLiteral("text"), text}};
}

QVariantMap item(int control, const QString& type, double x, double y, const QVariantList& keys, double rx = 0,
                 double ry = 0)
{
    return {{QStringLiteral("control"), control}, {QStringLiteral("type"), type}, {QStringLiteral("x"), x},
            {QStringLiteral("y"), y},             {QStringLiteral("rx"), rx},     {QStringLiteral("ry"), ry},
            {QStringLiteral("live"), true},       {QStringLiteral("keys"), keys}};
}

}

TEST_CASE("companion input: an input box on the phone enters typing and leaves it when it closes (ADR-0104)")
{
    InputController controller;
    const auto channel = std::make_shared<gs::app::ControlChannel>();
    controller.setTarget(QStringLiteral("phone"), channel, kNeverInFront);
    controller.setFocused(true);
    controller.setProfile(std::make_shared<const gs::input::Profile>());
    REQUIRE(controller.engaged());

    PhoneInputBox phone;
    CHECK_FALSE(phone.shown());
    CHECK_FALSE(phone.commits());

    const TypingChange opened = phone.changed(box(true, 0x4001, protocol::ImeAction::Send));
    CHECK(opened == TypingChange::Enter);
    gs::app::applyTyping(controller, QStringLiteral("phone"), opened);
    CHECK(controller.textInput());
    CHECK_FALSE(controller.engaged());
    CHECK(phone.commits());
    CHECK(phone.typingWanted());

    CHECK(phone.changed(box(true, 0x20001, protocol::ImeAction::Unspecified, true)) == TypingChange::None);
    CHECK(phone.box()->multiLine);

    const TypingChange closed = phone.changed(box(false, 0));
    CHECK(closed == TypingChange::Leave);
    gs::app::applyTyping(controller, QStringLiteral("phone"), closed);
    CHECK_FALSE(controller.textInput());
    CHECK(controller.engaged());
    CHECK_FALSE(phone.commits());

    CHECK(phone.changed(box(true)) == TypingChange::Enter);
    phone.left();
    CHECK(phone.commits());
    CHECK_FALSE(phone.typingWanted());
    CHECK(phone.changed(box(true, 2)) == TypingChange::None);
    CHECK_FALSE(phone.typingWanted());
    CHECK(phone.changed(box(false, 0)) == TypingChange::Leave);
    CHECK(phone.changed(box(true)) == TypingChange::Enter);
    CHECK(phone.typingWanted());

    gs::app::applyTyping(controller, QStringLiteral("phone"), TypingChange::Enter);
    REQUIRE(controller.textInput());
    gs::app::applyTyping(controller, QStringLiteral("another phone"), TypingChange::Leave);
    CHECK(controller.textInput());
    CHECK(phone.lost() == TypingChange::Leave);
    CHECK_FALSE(phone.shown());
    CHECK(phone.lost() == TypingChange::None);
}

TEST_CASE("companion input: without key mapping the box does not switch modes, typing already goes to the phone")
{
    InputController controller;
    controller.setTarget(QStringLiteral("phone"), std::make_shared<gs::app::ControlChannel>(), kNeverInFront);
    controller.setProfile(std::make_shared<const gs::input::Profile>());
    controller.setKeymap(false);
    PhoneInputBox phone;
    gs::app::applyTyping(controller, QStringLiteral("phone"), phone.changed(box(true)));
    CHECK_FALSE(controller.textInput());
    CHECK(phone.commits());
}

TEST_CASE("companion input: only Enter without modifiers goes to the companion, and the box reads as JSON for scripts")
{
    CHECK(gs::app::isPlainEnter(66, 0));
    CHECK(gs::app::isPlainEnter(160, 0));
    CHECK(gs::app::isPlainEnter(66, 0x00100000U));
    CHECK_FALSE(gs::app::isPlainEnter(66, 0x00000001U));
    CHECK_FALSE(gs::app::isPlainEnter(66, 0x00001000U));
    CHECK_FALSE(gs::app::isPlainEnter(66, 0x00000012U));
    CHECK_FALSE(gs::app::isPlainEnter(67, 0));

    const QJsonObject json =
        QJsonDocument::fromJson(QByteArray::fromStdString(gs::app::inputBoxJson(box(true, 0x4001, protocol::ImeAction::Send))))
            .object();
    CHECK(json.value(QStringLiteral("shown")).toBool());
    CHECK(json.value(QStringLiteral("inputType")).toInteger() == 0x4001);
    CHECK(json.value(QStringLiteral("action")).toString() == QStringLiteral("send"));
    CHECK_FALSE(json.value(QStringLiteral("multiLine")).toBool());
    CHECK(gs::app::inputBoxJson(box(false, 0)) ==
          R"({"action":"unspecified","inputType":0,"multiLine":false,"shown":false})");
}

TEST_CASE("companion input: the phone's key hints come from the mirror's hints, in picture ratios (ADR-0104)")
{
    const QSizeF picture(1000, 500);
    const QVariantList items{
        item(0, QStringLiteral("wasd"), 200, 300,
             {key(QStringLiteral("up"), QStringLiteral("W")), key(QStringLiteral("down"), QStringLiteral("S")),
              key(QStringLiteral("left"), QStringLiteral("A")), key(QStringLiteral("right"), QStringLiteral("D")),
              key(QStringLiteral("walk"), QStringLiteral("Shift")), key(QStringLiteral("run"), QStringLiteral("Ctrl"))},
             100, 100),
        item(1, QStringLiteral("camera"), 500, 250, {key(QStringLiteral("press"), QStringLiteral("`"))}),
        item(2, QStringLiteral("tap"), 900, 450,
             {key(QStringLiteral("press"), QStringLiteral("一二三四五六七八九十十一"))}),
        item(3, QStringLiteral("turbo"), 100, 100, {key(QStringLiteral("press"), QStringLiteral("F"))}),
        item(4, QStringLiteral("skill"), 700, 400, {key(QStringLiteral("press"), QStringLiteral("Q"))}),
    };
    const protocol::KeyHintLayout layout = gs::app::companionKeyHints(items, picture, {true, true, true, false, true});
    REQUIRE(layout.hints.size() == 9);
    CHECK(layout.hints[0].shape == protocol::KeyHintShape::Ring);
    CHECK(layout.hints[0].x == gs::input::fixedCoordinate(0.2));
    CHECK(layout.hints[0].y == gs::input::fixedCoordinate(0.6));
    CHECK(layout.hints[0].width == gs::input::fixedCoordinate(0.2));
    CHECK(layout.hints[0].height == gs::input::fixedCoordinate(0.4));
    CHECK(layout.hints[0].label.empty());
    CHECK(layout.hints[1] == protocol::KeyHint{.shape = protocol::KeyHintShape::Key,
                                               .x = gs::input::fixedCoordinate(0.2),
                                               .y = gs::input::fixedCoordinate(0.4),
                                               .label = "W"});
    CHECK(layout.hints[2].y == gs::input::fixedCoordinate(0.8));
    CHECK(layout.hints[3].x == gs::input::fixedCoordinate(0.1));
    CHECK(layout.hints[4].x == gs::input::fixedCoordinate(0.3));
    CHECK(layout.hints[5].label == "Shift Ctrl");
    CHECK(layout.hints[5].x == layout.hints[0].x);
    CHECK(layout.hints[6].shape == protocol::KeyHintShape::Camera);
    CHECK(layout.hints[6].label == "`");
    CHECK(layout.hints[7].shape == protocol::KeyHintShape::Tap);
    CHECK(layout.hints[7].label.size() <= protocol::kMaxKeyHintLabel);
    CHECK(layout.hints[7].label == std::string("一二三四五六七八九十"));
    CHECK(layout.hints[8].shape == protocol::KeyHintShape::Aim);
    CHECK(layout.hints[8].width == 0);

    CHECK(gs::app::companionKeyHints(items, QSizeF(), {}).hints.empty());
    QVariantList many;
    for (int i = 0; i < 300; ++i) {
        many.append(item(i, QStringLiteral("tap"), 10, 10, {key(QStringLiteral("press"), QStringLiteral("K"))}));
    }
    CHECK(gs::app::companionKeyHints(many, picture, {}).hints.size() == protocol::kMaxKeyHints);
}

TEST_CASE("companion input: the phone's hint state carries the resting opacity as a percentage")
{
    CHECK(gs::app::companionKeyHintState(true, true, 0.6) ==
          protocol::KeyHintState{.shown = true, .faded = true, .opacity = 60});
    CHECK(gs::app::companionKeyHintState(false, false, 1.7).opacity == 100);
    CHECK(gs::app::companionKeyHintState(true, false, -1.0).opacity == 0);
}

TEST_CASE("companion input: typing through the companion is on by default and its key hints are off, per phone")
{
    using namespace gs::app::settings;
    for (const std::string_view path : {std::string_view("companion.ime"), std::string_view("companion.hints")}) {
        CAPTURE(path);
        const Entry* entry = findSetting(path);
        REQUIRE(entry != nullptr);
        CHECK(entry->page == Page::Input);
        CHECK(entry->kind == Kind::Boolean);
        CHECK(entry->facets.scope == Scope::Device);
        CHECK(entry->facets.live);
    }
    CHECK(findSetting("companion.ime")->defaultValue == Value(true));
    CHECK(findSetting("companion.hints")->defaultValue == Value(false));
    CHECK(Settings{}.companion.ime);
    CHECK_FALSE(Settings{}.companion.hints);
    CHECK(save(Settings{}).find("\"companion\"") == std::string::npos);

    DeviceSettings device;
    CHECK(setOverride(device, "companion.ime", Value(false)));
    CHECK(setOverride(device, "companion.hints", Value(true)));
    CHECK_FALSE(effectiveSettings(Settings{}, device).companion.ime);
    CHECK(effectiveSettings(Settings{}, device).companion.hints);

    Devices devices;
    DeviceSettings paired;
    paired.companionPhone = std::string(32, 'a');
    paired.companionKey = std::string(64, 'b');
    paired.companionPc = std::string(32, 'c');
    devices.byId["usb-123"] = paired;
    const Loaded<Devices> loaded = load<Devices>(save(devices));
    REQUIRE(loaded.usable);
    CHECK(loaded.document.byId.at("usb-123") == paired);
    CHECK(save(Devices{}).find("companion") == std::string::npos);
}
