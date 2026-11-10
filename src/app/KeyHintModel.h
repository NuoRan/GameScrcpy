#pragma once

#include <gs/input/InputSource.h>
#include <gs/input/Profile.h>

#include <QList>
#include <QSizeF>
#include <QVariantList>

#include <optional>

namespace gs::app {

[[nodiscard]] QVariantList keyHintItems(const input::Profile& profile, QSizeF picture,
                                        const std::optional<input::Chord>& defaultLockKey, bool everyControl = false);

[[nodiscard]] bool crosshairWanted(const input::Profile& profile, const QList<bool>& live);

}
