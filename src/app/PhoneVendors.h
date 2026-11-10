#pragma once

#include <QCoreApplication>
#include <QString>

#include <array>
#include <span>
#include <string_view>

namespace gs::app {

struct VendorText {
    const char* source = nullptr;
    const char* comment = nullptr;
};

struct PhoneVendor {
    std::string_view id;
    std::array<std::string_view, 3> manufacturers;
    VendorText name;
    VendorText aboutPath;
    VendorText versionLabel;
    VendorText versionNote;
    VendorText developerPath;
    VendorText debugNote;
    VendorText inputSwitch;
    VendorText inputNote;
};

class PhoneVendors
{
    Q_DECLARE_TR_FUNCTIONS(PhoneVendors)

public:
    [[nodiscard]] static std::span<const PhoneVendor> all();
    [[nodiscard]] static const PhoneVendor& forManufacturer(std::string_view manufacturer);
    [[nodiscard]] static QString text(VendorText entry);
};

}
