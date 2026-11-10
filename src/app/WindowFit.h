#pragma once

#include "Settings.h"

#include <QList>
#include <QRect>
#include <QSize>

namespace gs::app {

[[nodiscard]] QRect fitWindow(const settings::WindowPlacement& saved, const QList<QRect>& screens, QSize fallback,
                              QSize minimum);

}
