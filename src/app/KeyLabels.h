#pragma once

#include <gs/input/InputSource.h>

#include <QString>

#include <vector>

namespace gs::app {

struct KeyLabel {
    QString text;
    bool mouse = false;
    QString glyph;
    QString prefix;
};

[[nodiscard]] KeyLabel keyLabel(const input::Chord& chord);
[[nodiscard]] std::vector<KeyLabel> keyCaps(const input::Chord& chord);

}
