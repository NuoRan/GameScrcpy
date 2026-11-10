#include "KeyHintModel.h"

#include "ComponentLibrary.h"
#include "KeyLabels.h"

#include <gs/input/KeyHints.h>

#include <QString>
#include <QVariantMap>

#include <string_view>

namespace gs::app {

namespace {

QString toQString(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

}

QVariantList keyHintItems(const input::Profile& profile, QSizeF picture,
                          const std::optional<input::Chord>& defaultLockKey, bool everyControl)
{
    QVariantList items;
    if (picture.isEmpty()) {
        return items;
    }
    const input::ScreenGeometry screen{.width = picture.width(), .height = picture.height()};
    for (const input::KeyHint& hint : input::keyHints(profile, screen, defaultLockKey, everyControl)) {
        QVariantList keys;
        for (const input::HintKey& key : hint.keys) {
            const KeyLabel label = keyLabel(key.input);
            keys.append(QVariantMap{{QStringLiteral("slot"), toQString(key.slot)},
                                    {QStringLiteral("text"), label.text},
                                    {QStringLiteral("mouse"), label.mouse},
                                    {QStringLiteral("glyph"), label.glyph},
                                    {QStringLiteral("prefix"), label.prefix}});
        }
        QVariantMap item{{QStringLiteral("control"), static_cast<int>(hint.control)},
                         {QStringLiteral("type"), toQString(hint.type)},
                         {QStringLiteral("x"), hint.center.x * picture.width()},
                         {QStringLiteral("y"), hint.center.y * picture.height()},
                         {QStringLiteral("rx"), hint.extent.x * picture.width()},
                         {QStringLiteral("ry"), hint.extent.y * picture.height()},
                         {QStringLiteral("live"), hint.live},
                         {QStringLiteral("keys"), keys}};
        QVariantList points;
        for (const input::HintPoint& point : hint.points) {
            points.append(QVariantMap{{QStringLiteral("name"), toQString(point.name)},
                                      {QStringLiteral("x"), point.at.x * picture.width()},
                                      {QStringLiteral("y"), point.at.y * picture.height()}});
        }
        if (!hint.component.empty()) {
            const QString ref = toQString(hint.component);
            const ComponentLibrary* library = ComponentLibrary::instance();
            const ComponentEntry* entry = library != nullptr ? library->find(ref) : nullptr;
            item.insert(QStringLiteral("component"), ref);
            item.insert(QStringLiteral("icon"), entry != nullptr ? entry->icon : QStringLiteral("sparkles"));
            item.insert(QStringLiteral("missing"), entry == nullptr);
            item.insert(QStringLiteral("points"), points);
        } else if (!points.isEmpty()) {
            item.insert(QStringLiteral("points"), points);
        }
        if (hint.cells > 0) {
            item.insert(QStringLiteral("cells"), hint.cells);
            item.insert(QStringLiteral("firstDeg"), hint.firstDeg);
        }
        if (hint.sprint > 0.0) {
            item.insert(QStringLiteral("sprint"), hint.sprint);
        }
        if (hint.castCenter) {
            item.insert(QStringLiteral("castX"), hint.castCenter->x * picture.width());
            item.insert(QStringLiteral("castY"), hint.castCenter->y * picture.height());
            item.insert(QStringLiteral("castRx"), hint.castExtent.x * picture.width());
            item.insert(QStringLiteral("castRy"), hint.castExtent.y * picture.height());
        }
        items.append(item);
    }
    return items;
}

bool crosshairWanted(const input::Profile& profile, const QList<bool>& live)
{
    for (std::size_t i = 0; i < profile.controls.size(); ++i) {
        const auto* camera = std::get_if<input::CameraControl>(&profile.controls[i].kind);
        if (camera == nullptr) {
            continue;
        }
        const auto at = static_cast<qsizetype>(i);
        const input::Control& control = profile.controls[i];
        if (at < live.size() ? live[at] : control.enabled && control.when.empty()) {
            return camera->crosshair;
        }
    }
    return false;
}

}
