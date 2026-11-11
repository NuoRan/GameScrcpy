#pragma once

#include <gs/input/InputSource.h>

#include <QString>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace gs::app {

struct KeyLabel {
    QString text;
    bool mouse = false;
    QString glyph;
    QString prefix;
};

struct ExtensionInputTitle {
    QString source;
    QString slot;

    friend bool operator==(const ExtensionInputTitle&, const ExtensionInputTitle&) = default;
};

using ExtensionInputTitles = std::map<std::string, ExtensionInputTitle, std::less<>>;

[[nodiscard]] KeyLabel keyLabel(const input::Chord& chord);
[[nodiscard]] std::vector<KeyLabel> keyCaps(const input::Chord& chord);
void setExtensionInputTitles(ExtensionInputTitles titles);

}
