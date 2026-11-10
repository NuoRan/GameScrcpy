#include "CompanionInput.h"

#include "InputController.h"

#include <gs/input/Geometry.h>
#include <gs/protocol/Enums.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace gs::app {

namespace {

bool isStick(const QString& type)
{
    return type == QStringLiteral("wasd") || type == QStringLiteral("rightWalk") || type == QStringLiteral("padStick");
}

protocol::KeyHintShape shapeOf(const QString& type)
{
    if (type == QStringLiteral("camera") || type == QStringLiteral("padCamera")) {
        return protocol::KeyHintShape::Camera;
    }
    if (type == QStringLiteral("skill")) {
        return protocol::KeyHintShape::Aim;
    }
    if (type == QStringLiteral("multiPress") || type == QStringLiteral("dial") || type == QStringLiteral("hold")) {
        return protocol::KeyHintShape::Hold;
    }
    if (type == QStringLiteral("freeLook")) {
        return protocol::KeyHintShape::FreeLook;
    }
    if (type == QStringLiteral("swipe")) {
        return protocol::KeyHintShape::Swipe;
    }
    if (type == QStringLiteral("longPress")) {
        return protocol::KeyHintShape::LongPress;
    }
    if (type == QStringLiteral("turbo")) {
        return protocol::KeyHintShape::Turbo;
    }
    return protocol::KeyHintShape::Tap;
}

std::string labelOf(const QString& text)
{
    std::string utf8 = text.toStdString();
    if (utf8.size() <= protocol::kMaxKeyHintLabel) {
        return utf8;
    }
    std::size_t size = protocol::kMaxKeyHintLabel;
    while (size > 0 && (static_cast<unsigned char>(utf8[size]) & 0xC0U) == 0x80U) {
        --size;
    }
    utf8.resize(size);
    return utf8;
}

QString textOf(const QVariant& key)
{
    return key.toMap().value(QStringLiteral("text")).toString();
}

}

TypingChange PhoneInputBox::changed(const protocol::InputBoxChanged& box)
{
    const bool was = shown();
    m_box = box;
    if (box.shown == was) {
        return TypingChange::None;
    }
    m_left = false;
    return box.shown ? TypingChange::Enter : TypingChange::Leave;
}

TypingChange PhoneInputBox::lost()
{
    const bool was = shown();
    m_box.reset();
    m_left = false;
    return was ? TypingChange::Leave : TypingChange::None;
}

void applyTyping(InputController& input, const QString& serial, TypingChange change)
{
    if (change == TypingChange::None || input.target() != serial) {
        return;
    }
    input.setTextInput(change == TypingChange::Enter);
}

bool isPlainEnter(int keycode, std::uint32_t metaState) noexcept
{
    return (keycode == kAndroidKeycodeEnter || keycode == kAndroidKeycodeNumpadEnter) &&
           (metaState & kAndroidModifierMeta) == 0U;
}

std::string inputBoxJson(const protocol::InputBoxChanged& box)
{
    const std::string_view action = protocol::enumName(box.action);
    const QJsonObject object{
        {QStringLiteral("shown"), box.shown},
        {QStringLiteral("inputType"), static_cast<qint64>(box.inputType)},
        {QStringLiteral("action"), QString::fromUtf8(action.data(), static_cast<qsizetype>(action.size()))},
        {QStringLiteral("multiLine"), box.multiLine},
    };
    return QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString();
}

protocol::KeyHintLayout companionKeyHints(const QVariantList& items, QSizeF picture, const QList<bool>& live)
{
    protocol::KeyHintLayout layout;
    if (picture.isEmpty()) {
        return layout;
    }
    const auto add = [&layout](protocol::KeyHintShape shape, double x, double y, double width, double height,
                               const QString& label) {
        if (layout.hints.size() >= protocol::kMaxKeyHints) {
            return;
        }
        layout.hints.push_back(protocol::KeyHint{.shape = shape,
                                                 .x = input::fixedCoordinate(x),
                                                 .y = input::fixedCoordinate(y),
                                                 .width = input::fixedCoordinate(width),
                                                 .height = input::fixedCoordinate(height),
                                                 .label = labelOf(label)});
    };
    for (const QVariant& entry : items) {
        const QVariantMap item = entry.toMap();
        const int control = item.value(QStringLiteral("control")).toInt();
        const bool on = control >= 0 && control < live.size() ? live[control]
                                                              : item.value(QStringLiteral("live"), true).toBool();
        if (!on) {
            continue;
        }
        const QString type = item.value(QStringLiteral("type")).toString();
        const double x = item.value(QStringLiteral("x")).toDouble() / picture.width();
        const double y = item.value(QStringLiteral("y")).toDouble() / picture.height();
        const QVariantList keys = item.value(QStringLiteral("keys")).toList();
        const QString lead = keys.isEmpty() ? QString() : textOf(keys.front());
        if (isStick(type) || type == QStringLiteral("dial")) {
            const double rx = item.value(QStringLiteral("rx")).toDouble() / picture.width();
            const double ry = item.value(QStringLiteral("ry")).toDouble() / picture.height();
            add(protocol::KeyHintShape::Ring, x, y, 2 * rx, 2 * ry, {});
            if (type == QStringLiteral("dial")) {
                add(protocol::KeyHintShape::Hold, x, y, 0, 0, lead);
                continue;
            }
            QStringList extras;
            for (const QVariant& key : keys) {
                const QVariantMap map = key.toMap();
                const QString slot = map.value(QStringLiteral("slot")).toString();
                const QString text = map.value(QStringLiteral("text")).toString();
                if (slot == QStringLiteral("up")) {
                    add(protocol::KeyHintShape::Key, x, y - ry, 0, 0, text);
                } else if (slot == QStringLiteral("down")) {
                    add(protocol::KeyHintShape::Key, x, y + ry, 0, 0, text);
                } else if (slot == QStringLiteral("left")) {
                    add(protocol::KeyHintShape::Key, x - rx, y, 0, 0, text);
                } else if (slot == QStringLiteral("right")) {
                    add(protocol::KeyHintShape::Key, x + rx, y, 0, 0, text);
                } else if (!text.isEmpty()) {
                    extras.append(text);
                }
            }
            if (!extras.isEmpty()) {
                add(protocol::KeyHintShape::Key, x, y, 0, 0, extras.join(QLatin1Char(' ')));
            }
            continue;
        }
        if (item.value(QStringLiteral("unbound")).toBool()) {
            add(protocol::KeyHintShape::Unbound, x, y, 0, 0, item.value(QStringLiteral("name")).toString());
            continue;
        }
        if (type == QStringLiteral("pinch")) {
            QStringList zooms;
            for (const QVariant& key : keys) {
                const QString slot = key.toMap().value(QStringLiteral("slot")).toString();
                if (slot == QStringLiteral("zoomIn") || slot == QStringLiteral("zoomOut")) {
                    zooms.append(textOf(key));
                }
            }
            add(protocol::KeyHintShape::Key, x, y, 0, 0, zooms.join(QLatin1Char('/')));
            continue;
        }
        add(shapeOf(type), x, y, 0, 0, lead);
    }
    return layout;
}

protocol::KeyHintState companionKeyHintState(bool shown, bool faded, double opacity) noexcept
{
    const double percent = std::isfinite(opacity) ? std::round(std::clamp(opacity, 0.0, 1.0) * 100.0) : 100.0;
    return protocol::KeyHintState{.shown = shown, .faded = faded, .opacity = static_cast<std::uint8_t>(percent)};
}

}
