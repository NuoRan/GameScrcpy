#include "PictureTouch.h"
#include "Settings.h"

#include <doctest/doctest.h>

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <vector>

using gs::app::assignedToPhone;
using gs::app::assignmentName;
using gs::app::CaptureAssignment;
using gs::app::CaptureListing;
using gs::app::captureListing;
using gs::app::effectivePicture;
using gs::app::ExtensionTouchOffer;
using gs::app::LinkChoice;
using gs::app::LinkChoices;
using gs::app::linkChoices;
using gs::app::PhoneLinks;
using gs::app::PictureInUse;
using gs::app::pictureChoiceList;
using gs::app::pictureSourceName;
using gs::app::touchChoiceList;
using gs::app::settings::PictureSource;

namespace {

const QString kNeedsAdb = QStringLiteral("Needs USB debugging on a cable or wireless debugging");
const QString kNeedsCard =
    QStringLiteral("No capture card shows this phone yet: plug one in and choose this phone on its card");
const QString kNeedsAoa = QStringLiteral("Needs this phone on a USB cable with the WinUSB driver");
const QString kExtensionOff = QStringLiteral("The extension that adds it is off");

QStringList idsOf(const std::vector<LinkChoice>& choices)
{
    QStringList ids;
    for (const LinkChoice& choice : choices) {
        ids.append(choice.id);
    }
    return ids;
}

const LinkChoice& choiceOf(const std::vector<LinkChoice>& choices, const QString& id)
{
    const auto found = std::ranges::find(choices, id, &LinkChoice::id);
    REQUIRE(found != choices.end());
    return *found;
}

void checkOpen(const LinkChoice& choice)
{
    CAPTURE(choice.id.toStdString());
    CHECK(choice.enabled);
    CHECK(choice.reason.isEmpty());
}

void checkGrey(const LinkChoice& choice, const QString& reason)
{
    CAPTURE(choice.id.toStdString());
    CHECK_FALSE(choice.enabled);
    CHECK(choice.reason == reason);
}

}

TEST_CASE("ADR-0108: an online phone with adb only offers phone mirroring, Normal and UHID, and greys the capture "
          "card and AOA")
{
    const LinkChoices choices = linkChoices({.online = true, .adb = true});
    CHECK(idsOf(choices.picture) ==
          QStringList{QStringLiteral("phone"), QStringLiteral("capture"), QStringLiteral("none")});
    CHECK(idsOf(choices.touch) == QStringList{QStringLiteral("inject"), QStringLiteral("uhid"),
                                              QStringLiteral("aoa"), QStringLiteral("none")});
    CHECK(choiceOf(choices.picture, QStringLiteral("phone")).label == QStringLiteral("Phone mirroring"));
    CHECK(choiceOf(choices.picture, QStringLiteral("capture")).label == QStringLiteral("Capture card"));
    CHECK(choiceOf(choices.picture, QStringLiteral("none")).label == QStringLiteral("No picture"));
    checkOpen(choiceOf(choices.picture, QStringLiteral("phone")));
    checkGrey(choiceOf(choices.picture, QStringLiteral("capture")), kNeedsCard);
    checkOpen(choiceOf(choices.picture, QStringLiteral("none")));
    checkOpen(choiceOf(choices.touch, QStringLiteral("inject")));
    checkOpen(choiceOf(choices.touch, QStringLiteral("uhid")));
    checkGrey(choiceOf(choices.touch, QStringLiteral("aoa")), kNeedsAoa);
    checkOpen(choiceOf(choices.touch, QStringLiteral("none")));
}

TEST_CASE("ADR-0108: a phone with adb and a capture card given to it offers the card by its name")
{
    const LinkChoices choices =
        linkChoices({.online = true, .adb = true, .captureCard = true, .captureName = QStringLiteral("UGREEN 25854")});
    const LinkChoice& card = choiceOf(choices.picture, QStringLiteral("capture"));
    CHECK(card.label == QStringLiteral("Capture card · UGREEN 25854"));
    checkOpen(card);
    checkOpen(choiceOf(choices.picture, QStringLiteral("phone")));
    checkOpen(choiceOf(choices.touch, QStringLiteral("inject")));
}

TEST_CASE("ADR-0108: a phone known only on USB with the WinUSB driver offers AOA and greys what needs adb")
{
    const LinkChoices choices = linkChoices({.online = true, .aoa = true});
    checkGrey(choiceOf(choices.picture, QStringLiteral("phone")), kNeedsAdb);
    checkGrey(choiceOf(choices.picture, QStringLiteral("capture")), kNeedsCard);
    checkOpen(choiceOf(choices.picture, QStringLiteral("none")));
    checkGrey(choiceOf(choices.touch, QStringLiteral("inject")), kNeedsAdb);
    checkGrey(choiceOf(choices.touch, QStringLiteral("uhid")), kNeedsAdb);
    checkOpen(choiceOf(choices.touch, QStringLiteral("aoa")));
    checkOpen(choiceOf(choices.touch, QStringLiteral("none")));

    const LinkChoices withCard = linkChoices(
        {.online = true, .captureCard = true, .captureName = QStringLiteral("Cam Link 4K"), .aoa = true});
    checkOpen(choiceOf(withCard.picture, QStringLiteral("capture")));
    checkGrey(choiceOf(withCard.touch, QStringLiteral("inject")), kNeedsAdb);
}

TEST_CASE("ADR-0108: the demo device counts as having adb")
{
    const LinkChoices choices = linkChoices({.online = true, .demo = true});
    checkOpen(choiceOf(choices.picture, QStringLiteral("phone")));
    checkOpen(choiceOf(choices.touch, QStringLiteral("inject")));
    checkOpen(choiceOf(choices.touch, QStringLiteral("uhid")));
    checkGrey(choiceOf(choices.touch, QStringLiteral("aoa")), kNeedsAoa);
    checkGrey(choiceOf(choices.picture, QStringLiteral("capture")), kNeedsCard);
}

TEST_CASE("ADR-0108: nothing is greyed while the phone is not online")
{
    const LinkChoices choices = linkChoices(
        {.extensions = {ExtensionTouchOffer{.key = QStringLiteral("gamescrcpy.esp32/esp32"), .on = false}}});
    for (const LinkChoice& choice : choices.picture) {
        checkOpen(choice);
    }
    for (const LinkChoice& choice : choices.touch) {
        checkOpen(choice);
    }
    CHECK(choices.touch.size() == 5);
}

TEST_CASE("ADR-0108: a touch method from an extension is listed by its key and greyed while its extension is off")
{
    const LinkChoices choices =
        linkChoices({.online = true,
                     .adb = true,
                     .extensions = {ExtensionTouchOffer{.key = QStringLiteral("alice.box/box"), .on = true},
                                    ExtensionTouchOffer{.key = QStringLiteral("gamescrcpy.esp32/esp32"), .on = false}}});
    CHECK(idsOf(choices.touch) == QStringList{QStringLiteral("inject"), QStringLiteral("uhid"), QStringLiteral("aoa"),
                                              QStringLiteral("none"), QStringLiteral("alice.box/box"),
                                              QStringLiteral("gamescrcpy.esp32/esp32")});
    checkOpen(choiceOf(choices.touch, QStringLiteral("alice.box/box")));
    checkGrey(choiceOf(choices.touch, QStringLiteral("gamescrcpy.esp32/esp32")), kExtensionOff);
}

TEST_CASE("ADR-0108: the choice lists for QML carry ids, labels only for pictures, whether enabled and the reason")
{
    const LinkChoices choices = linkChoices({.online = true, .adb = true});
    const QVariantList pictures = pictureChoiceList(choices.picture);
    REQUIRE(pictures.size() == 3);
    const QVariantMap card = pictures.at(1).toMap();
    CHECK(card.value(QStringLiteral("id")).toString() == QStringLiteral("capture"));
    CHECK(card.value(QStringLiteral("label")).toString() == QStringLiteral("Capture card"));
    CHECK_FALSE(card.value(QStringLiteral("enabled")).toBool());
    CHECK(card.value(QStringLiteral("reason")).toString() == kNeedsCard);

    const QVariantList touches = touchChoiceList(choices.touch);
    REQUIRE(touches.size() == 4);
    const QVariantMap aoa = touches.at(2).toMap();
    CHECK(aoa.value(QStringLiteral("id")).toString() == QStringLiteral("aoa"));
    CHECK_FALSE(aoa.contains(QStringLiteral("label")));
    CHECK_FALSE(aoa.value(QStringLiteral("enabled")).toBool());
    CHECK(aoa.value(QStringLiteral("reason")).toString() == kNeedsAoa);
    CHECK(touches.at(0).toMap().value(QStringLiteral("enabled")).toBool());
    CHECK(touches.at(0).toMap().value(QStringLiteral("reason")).toString().isEmpty());
}

TEST_CASE("ADR-0108: a capture card asks until decided, stays on its own when told so, and joins the card of a "
          "phone that is there")
{
    CHECK(captureListing("", false) == CaptureListing{.assignment = CaptureAssignment::Ask, .listed = true});
    CHECK(captureListing("", true) == CaptureListing{.assignment = CaptureAssignment::Ask, .listed = true});
    CHECK(captureListing("standalone", true) ==
          CaptureListing{.assignment = CaptureAssignment::Standalone, .listed = true});
    CHECK(captureListing("R58M123ABC", true) ==
          CaptureListing{.assignment = CaptureAssignment::Assigned, .listed = false});
    CHECK(captureListing("R58M123ABC", false) ==
          CaptureListing{.assignment = CaptureAssignment::Assigned, .listed = true});
    CHECK(assignmentName(CaptureAssignment::Ask) == QStringLiteral("ask"));
    CHECK(assignmentName(CaptureAssignment::Standalone) == QStringLiteral("standalone"));
    CHECK(assignmentName(CaptureAssignment::Assigned) == QStringLiteral("assigned"));
    CHECK_FALSE(assignedToPhone(""));
    CHECK_FALSE(assignedToPhone("standalone"));
    CHECK(assignedToPhone("gs-demo"));
}

TEST_CASE("ADR-0108: the picture in use falls back from a missing capture card to the phone when it has adb, and "
          "says why")
{
    const PictureInUse card =
        effectivePicture(PictureSource::Capture, {.online = true, .adb = true, .captureCard = true});
    CHECK(card.source == PictureSource::Capture);
    CHECK(card.note.isEmpty());

    const PictureInUse phone = effectivePicture(PictureSource::Capture, {.online = true, .adb = true});
    CHECK(phone.source == PictureSource::Phone);
    CHECK(phone.note == QStringLiteral("The capture card is not connected, so the picture comes from the phone"));

    const PictureInUse demo = effectivePicture(PictureSource::Capture, {.online = true, .demo = true});
    CHECK(demo.source == PictureSource::Phone);

    const PictureInUse nothing = effectivePicture(PictureSource::Capture, {.online = true, .aoa = true});
    CHECK(nothing.source == PictureSource::None);
    CHECK(nothing.note == QStringLiteral("The capture card is not connected, so there is no picture"));

    const PictureInUse cardWithoutAdb =
        effectivePicture(PictureSource::Capture, {.online = true, .captureCard = true, .aoa = true});
    CHECK(cardWithoutAdb.source == PictureSource::Capture);
}

TEST_CASE("ADR-0108: phone mirroring needs adb, and no picture stays no picture")
{
    const PictureInUse mirrored = effectivePicture(PictureSource::Phone, {.online = true, .adb = true});
    CHECK(mirrored.source == PictureSource::Phone);
    CHECK(mirrored.note.isEmpty());

    const PictureInUse aoaOnly = effectivePicture(PictureSource::Phone, {.online = true, .aoa = true});
    CHECK(aoaOnly.source == PictureSource::None);
    CHECK(aoaOnly.note == QStringLiteral("Phone mirroring needs USB debugging, so there is no picture"));

    const PictureInUse none = effectivePicture(PictureSource::None, {.online = true, .adb = true, .captureCard = true});
    CHECK(none.source == PictureSource::None);
    CHECK(none.note.isEmpty());

    CHECK(pictureSourceName(PictureSource::Phone) == QStringLiteral("phone"));
    CHECK(pictureSourceName(PictureSource::Capture) == QStringLiteral("capture"));
    CHECK(pictureSourceName(PictureSource::None) == QStringLiteral("none"));
}
