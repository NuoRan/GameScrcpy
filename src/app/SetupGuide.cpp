#include "SetupGuide.h"

#include "PhoneVendors.h"

#include <QVariantMap>

namespace gs::app {

namespace {

QString idOf(const PhoneVendor& vendor)
{
    return QString::fromLatin1(vendor.id.data(), static_cast<qsizetype>(vendor.id.size()));
}

QVariant textOf(VendorText text)
{
    if (text.source == nullptr) {
        return {};
    }
    return QVariantMap{{QStringLiteral("source"), QString::fromUtf8(text.source)},
                       {QStringLiteral("comment"), QString::fromUtf8(text.comment)}};
}

}

SetupGuide::SetupGuide(QObject* parent)
    : QObject(parent)
{
}

QVariantList SetupGuide::vendors() const
{
    QVariantList list;
    for (const PhoneVendor& vendor : PhoneVendors::all()) {
        list.append(QVariantMap{{QStringLiteral("id"), idOf(vendor)},
                                {QStringLiteral("name"), textOf(vendor.name)},
                                {QStringLiteral("aboutPath"), textOf(vendor.aboutPath)},
                                {QStringLiteral("versionLabel"), textOf(vendor.versionLabel)},
                                {QStringLiteral("versionNote"), textOf(vendor.versionNote)},
                                {QStringLiteral("developerPath"), textOf(vendor.developerPath)},
                                {QStringLiteral("debugNote"), textOf(vendor.debugNote)},
                                {QStringLiteral("inputSwitch"), textOf(vendor.inputSwitch)},
                                {QStringLiteral("inputNote"), textOf(vendor.inputNote)}});
    }
    return list;
}

QString SetupGuide::vendorFor(const QString& manufacturer) const
{
    return idOf(PhoneVendors::forManufacturer(manufacturer.toStdString()));
}

void SetupGuide::open(int page, const QString& vendor, const QString& step)
{
    emit openRequested(page, vendor, step);
}

}
