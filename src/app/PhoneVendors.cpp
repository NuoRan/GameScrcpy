#include "PhoneVendors.h"

#include <algorithm>
#include <cctype>

namespace gs::app {

namespace {

constexpr std::array kVendors{
    PhoneVendor{
        .id = "xiaomi",
        .manufacturers = {"xiaomi", "redmi", "poco"},
        .name = QT_TRANSLATE_NOOP3("PhoneVendors", "Xiaomi · Redmi · POCO", "Xiaomi"),
        .aboutPath = QT_TRANSLATE_NOOP3("PhoneVendors", "Settings > My device > All specs", "Xiaomi"),
        .versionLabel = QT_TRANSLATE_NOOP3("PhoneVendors", "OS version", "Xiaomi"),
        .versionNote = QT_TRANSLATE_NOOP3("PhoneVendors", "On MIUI it is “MIUI version”.", "Xiaomi"),
        .developerPath = QT_TRANSLATE_NOOP3("PhoneVendors", "Settings > Additional settings > Developer options",
                                            "Xiaomi"),
        .inputSwitch = QT_TRANSLATE_NOOP3("PhoneVendors", "USB debugging (Security settings)", "Xiaomi"),
        .inputNote = QT_TRANSLATE_NOOP3("PhoneVendors",
                                        "It needs a signed-in Mi account; restart the phone after turning it on.",
                                        "Xiaomi"),
    },
    PhoneVendor{
        .id = "huawei",
        .manufacturers = {"huawei", "", ""},
        .name = QT_TRANSLATE_NOOP3("PhoneVendors", "Huawei", "Huawei"),
        .aboutPath = QT_TRANSLATE_NOOP3("PhoneVendors", "Settings > About phone", "Huawei"),
        .versionLabel = QT_TRANSLATE_NOOP3("PhoneVendors", "HarmonyOS version", "Huawei"),
        .versionNote = QT_TRANSLATE_NOOP3("PhoneVendors", "On EMUI it is “Build number”.", "Huawei"),
        .developerPath = QT_TRANSLATE_NOOP3("PhoneVendors", "Settings > System & updates > Developer options",
                                            "Huawei"),
        .debugNote = QT_TRANSLATE_NOOP3("PhoneVendors",
                                        "Also turn on “Allow ADB debugging in charge only mode”. HarmonyOS 5 and "
                                        "later is not Android and cannot be mirrored.",
                                        "Huawei"),
    },
    PhoneVendor{
        .id = "honor",
        .manufacturers = {"honor", "", ""},
        .name = QT_TRANSLATE_NOOP3("PhoneVendors", "Honor", "Honor"),
        .aboutPath = QT_TRANSLATE_NOOP3("PhoneVendors", "Settings > About phone", "Honor"),
        .versionLabel = QT_TRANSLATE_NOOP3("PhoneVendors", "Build number", "Honor"),
        .developerPath = QT_TRANSLATE_NOOP3("PhoneVendors", "Settings > System & updates > Developer options",
                                            "Honor"),
        .debugNote = QT_TRANSLATE_NOOP3("PhoneVendors", "Also turn on “Allow ADB debugging in charge only mode”.",
                                        "Honor"),
    },
    PhoneVendor{
        .id = "oppo",
        .manufacturers = {"oppo", "oneplus", "realme"},
        .name = QT_TRANSLATE_NOOP3("PhoneVendors", "OPPO · OnePlus · realme", "OPPO"),
        .aboutPath = QT_TRANSLATE_NOOP3("PhoneVendors", "Settings > About device > Version", "OPPO"),
        .versionLabel = QT_TRANSLATE_NOOP3("PhoneVendors", "Build number", "OPPO"),
        .developerPath = QT_TRANSLATE_NOOP3("PhoneVendors", "Settings > Additional settings > Developer options",
                                            "OPPO"),
        .debugNote = QT_TRANSLATE_NOOP3("PhoneVendors", "On some versions it is under System settings instead.",
                                        "OPPO"),
        .inputSwitch = QT_TRANSLATE_NOOP3("PhoneVendors", "Disable permission monitoring", "OPPO"),
        .inputNote = QT_TRANSLATE_NOOP3("PhoneVendors",
                                        "From ColorOS 16 it only shows while the phone's language is English, as "
                                        "“Disable system optimization”; switch the language back afterwards.",
                                        "OPPO"),
    },
    PhoneVendor{
        .id = "vivo",
        .manufacturers = {"vivo", "iqoo", ""},
        .name = QT_TRANSLATE_NOOP3("PhoneVendors", "vivo · iQOO", "vivo"),
        .aboutPath = QT_TRANSLATE_NOOP3("PhoneVendors", "Settings > System management > About phone > Version info",
                                        "vivo"),
        .versionLabel = QT_TRANSLATE_NOOP3("PhoneVendors", "Software version", "vivo"),
        .developerPath = QT_TRANSLATE_NOOP3("PhoneVendors", "Settings > System management > Developer options",
                                            "vivo"),
        .inputSwitch = QT_TRANSLATE_NOOP3("PhoneVendors", "USB simulate click", "vivo"),
    },
    PhoneVendor{
        .id = "samsung",
        .manufacturers = {"samsung", "", ""},
        .name = QT_TRANSLATE_NOOP3("PhoneVendors", "Samsung", "Samsung"),
        .aboutPath = QT_TRANSLATE_NOOP3("PhoneVendors", "Settings > About phone > Software information", "Samsung"),
        .versionLabel = QT_TRANSLATE_NOOP3("PhoneVendors", "Build number", "Samsung"),
        .developerPath = QT_TRANSLATE_NOOP3("PhoneVendors", "Settings > Developer options", "Samsung"),
        .debugNote = QT_TRANSLATE_NOOP3("PhoneVendors",
                                        "Developer options is at the bottom of Settings. If “USB debugging” is "
                                        "greyed out, first turn off Settings > Security and privacy > Auto Blocker.",
                                        "Samsung"),
    },
    PhoneVendor{
        .id = "other",
        .manufacturers = {"", "", ""},
        .name = QT_TRANSLATE_NOOP3("PhoneVendors", "Other phones", "Other"),
        .aboutPath = QT_TRANSLATE_NOOP3("PhoneVendors", "Settings > About phone", "Other"),
        .versionLabel = QT_TRANSLATE_NOOP3("PhoneVendors", "Build number", "Other"),
        .developerPath = QT_TRANSLATE_NOOP3("PhoneVendors", "Settings > System > Developer options", "Other"),
    },
};

bool sameName(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

}

std::span<const PhoneVendor> PhoneVendors::all()
{
    return kVendors;
}

const PhoneVendor& PhoneVendors::forManufacturer(std::string_view manufacturer)
{
    const auto found = std::ranges::find_if(kVendors, [&](const PhoneVendor& vendor) {
        return std::ranges::any_of(vendor.manufacturers, [&](std::string_view name) {
            return !name.empty() && sameName(name, manufacturer);
        });
    });
    return found != kVendors.end() ? *found : kVendors.back();
}

QString PhoneVendors::text(VendorText entry)
{
    return entry.source != nullptr ? QCoreApplication::translate("PhoneVendors", entry.source, entry.comment)
                                   : QString();
}

}
