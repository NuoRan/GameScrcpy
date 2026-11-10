#pragma once

#include "Settings.h"

#include <QString>
#include <QVariantList>

#include <cstdint>
#include <string_view>
#include <vector>

namespace gs::app {

inline constexpr char kStandaloneCapture[] = "standalone";

struct ExtensionTouchOffer {
    QString key;
    bool on = false;

    friend bool operator==(const ExtensionTouchOffer&, const ExtensionTouchOffer&) = default;
};

struct PhoneLinks {
    bool online = false;
    bool adb = false;
    bool demo = false;
    bool captureCard = false;
    QString captureName;
    bool aoa = false;
    std::vector<ExtensionTouchOffer> extensions;
};

struct LinkChoice {
    QString id;
    QString label;
    bool enabled = true;
    QString reason;

    friend bool operator==(const LinkChoice&, const LinkChoice&) = default;
};

struct LinkChoices {
    std::vector<LinkChoice> picture;
    std::vector<LinkChoice> touch;
};

[[nodiscard]] LinkChoices linkChoices(const PhoneLinks& phone);
[[nodiscard]] QVariantList pictureChoiceList(const std::vector<LinkChoice>& choices);
[[nodiscard]] QVariantList touchChoiceList(const std::vector<LinkChoice>& choices);

enum class CaptureAssignment : std::uint8_t { Ask, Standalone, Assigned };

struct CaptureListing {
    CaptureAssignment assignment = CaptureAssignment::Ask;
    bool listed = true;

    friend bool operator==(const CaptureListing&, const CaptureListing&) = default;
};

[[nodiscard]] CaptureListing captureListing(std::string_view phone, bool phonePresent);
[[nodiscard]] QString assignmentName(CaptureAssignment assignment);
[[nodiscard]] bool assignedToPhone(std::string_view phone) noexcept;

struct PictureInUse {
    settings::PictureSource source = settings::PictureSource::Phone;
    QString note;
};

[[nodiscard]] PictureInUse effectivePicture(settings::PictureSource wanted, const PhoneLinks& phone);
[[nodiscard]] QString pictureSourceName(settings::PictureSource source);

}
