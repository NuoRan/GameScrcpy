#include "PictureTouch.h"

#include <QCoreApplication>
#include <QVariantMap>

#include <cstddef>
#include <utility>

namespace gs::app {

namespace {

QString needsAdb()
{
    return QCoreApplication::translate("gs::app::PictureTouch", "Needs USB debugging on a cable or wireless debugging");
}

LinkChoice makeChoice(QString id, QString label, bool enabled, const QString& reason)
{
    return {.id = std::move(id), .label = std::move(label), .enabled = enabled, .reason = enabled ? QString() : reason};
}

}

LinkChoices linkChoices(const PhoneLinks& phone)
{
    const bool unknown = !phone.online;
    const bool adb = unknown || phone.adb || phone.demo;
    LinkChoices choices;
    choices.picture.push_back(makeChoice(QStringLiteral("phone"),
                                         QCoreApplication::translate("gs::app::PictureTouch", "Phone mirroring"), adb,
                                         needsAdb()));
    const QString card = phone.captureCard
                             ? QCoreApplication::translate("gs::app::PictureTouch", "Capture card · %1")
                                   .arg(phone.captureName)
                             : QCoreApplication::translate("gs::app::PictureTouch", "Capture card");
    choices.picture.push_back(makeChoice(QStringLiteral("capture"), card, unknown || phone.captureCard,
                                         QCoreApplication::translate("gs::app::PictureTouch",
                                                                     "No capture card shows this phone yet: plug one "
                                                                     "in and choose this phone on its card")));
    choices.picture.push_back(makeChoice(QStringLiteral("none"),
                                         QCoreApplication::translate("gs::app::PictureTouch", "No picture"), true, {}));
    choices.touch.push_back(makeChoice(QStringLiteral("inject"), {}, adb, needsAdb()));
    choices.touch.push_back(makeChoice(QStringLiteral("uhid"), {}, adb, needsAdb()));
    choices.touch.push_back(makeChoice(QStringLiteral("aoa"), {}, unknown || phone.aoa,
                                       QCoreApplication::translate("gs::app::PictureTouch",
                                                                   "Needs this phone on a USB cable with the WinUSB "
                                                                   "driver")));
    choices.touch.push_back(makeChoice(QStringLiteral("none"), {}, true, {}));
    for (const ExtensionTouchOffer& offer : phone.extensions) {
        choices.touch.push_back(
            makeChoice(offer.key, {}, unknown || offer.on,
                       QCoreApplication::translate("gs::app::PictureTouch", "The extension that adds it is off")));
    }
    return choices;
}

QVariantList pictureChoiceList(const std::vector<LinkChoice>& choices)
{
    QVariantList list;
    for (const LinkChoice& each : choices) {
        list.append(QVariantMap{{QStringLiteral("id"), each.id},
                                {QStringLiteral("label"), each.label},
                                {QStringLiteral("enabled"), each.enabled},
                                {QStringLiteral("reason"), each.reason}});
    }
    return list;
}

QVariantList touchChoiceList(const std::vector<LinkChoice>& choices)
{
    QVariantList list;
    for (const LinkChoice& each : choices) {
        list.append(QVariantMap{{QStringLiteral("id"), each.id},
                                {QStringLiteral("enabled"), each.enabled},
                                {QStringLiteral("reason"), each.reason}});
    }
    return list;
}

bool assignedToPhone(std::string_view phone) noexcept
{
    return !phone.empty() && phone != kStandaloneCapture;
}

CaptureListing captureListing(std::string_view phone, bool phonePresent)
{
    if (phone.empty()) {
        return {.assignment = CaptureAssignment::Ask, .listed = true};
    }
    if (!assignedToPhone(phone)) {
        return {.assignment = CaptureAssignment::Standalone, .listed = true};
    }
    return {.assignment = CaptureAssignment::Assigned, .listed = !phonePresent};
}

QString assignmentName(CaptureAssignment assignment)
{
    switch (assignment) {
    case CaptureAssignment::Standalone:
        return QStringLiteral("standalone");
    case CaptureAssignment::Assigned:
        return QStringLiteral("assigned");
    case CaptureAssignment::Ask:
        break;
    }
    return QStringLiteral("ask");
}

PictureInUse effectivePicture(settings::PictureSource wanted, const PhoneLinks& phone)
{
    const bool adb = phone.adb || phone.demo;
    switch (wanted) {
    case settings::PictureSource::Capture:
        if (phone.captureCard) {
            return {.source = settings::PictureSource::Capture};
        }
        if (adb) {
            return {.source = settings::PictureSource::Phone,
                    .note = QCoreApplication::translate("gs::app::PictureTouch",
                                                        "The capture card is not connected, so the picture comes "
                                                        "from the phone")};
        }
        return {.source = settings::PictureSource::None,
                .note = QCoreApplication::translate("gs::app::PictureTouch",
                                                    "The capture card is not connected, so there is no picture")};
    case settings::PictureSource::None:
        return {.source = settings::PictureSource::None};
    case settings::PictureSource::Phone:
        break;
    }
    if (adb) {
        return {.source = settings::PictureSource::Phone};
    }
    return {.source = settings::PictureSource::None,
            .note = QCoreApplication::translate("gs::app::PictureTouch",
                                                "Phone mirroring needs USB debugging, so there is no picture")};
}

QString pictureSourceName(settings::PictureSource source)
{
    const std::string_view name = settings::kPictureSourceNames[static_cast<std::size_t>(source)];
    return QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()));
}

}
