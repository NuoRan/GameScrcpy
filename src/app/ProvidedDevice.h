#pragma once

#include <QString>
#include <QVariantList>

#include <string>

namespace gs::app {

struct ProvidedDevice {
    std::string extension;
    QString extensionName;
    std::string provider;
    std::string id;
    QString name;
    QString kind;
    QString summary;
    QString state;
    QString stateText;
    QString icon;
    QString iconPath;
    std::string touchMethod;
    std::string resource;
    bool capture = false;
    QVariantList actions;

    friend bool operator==(const ProvidedDevice&, const ProvidedDevice&) = default;
};

}
