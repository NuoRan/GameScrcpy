#include "ProfileEditor.h"

#include "ComponentDefaults.h"
#include "ComponentLibrary.h"
#include "DraftDiff.h"
#include "KeyHintModel.h"
#include "KeyLabels.h"
#include "NativeExtensions.h"
#include "ProfileForm.h"

#include <gs/blueprint/Layout.h>
#include <gs/input/DeviceEvents.h>
#include <gs/platform/AtomicFile.h>

#include <nlohmann/json.hpp>

#include <QDir>
#include <QSizeF>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <numbers>
#include <optional>
#include <ranges>
#include <string>
#include <utility>

namespace gs::app {

namespace {

using Json = nlohmann::json;

constexpr double kDuplicateStep = 0.03;
constexpr std::size_t kMostPreviewBytes = std::size_t{2} << 20;
std::optional<QString> readPreviewText(const QString& folder, const QString& path)
{
    if (folder.isEmpty() || path.isEmpty() || path.contains(u"..") || path.startsWith(u'/') || path.contains(u':')) {
        return std::nullopt;
    }
    std::string contents;
    const QString file = folder + u'/' + path;
    if (platform::readFile(std::filesystem::path{file.toStdWString()}, kMostPreviewBytes, contents,
                           platform::WhenBusy::GiveUp)) {
        return std::nullopt;
    }
    return QString::fromStdString(contents);
}

struct CopiedKey {
    input::Control control;
    input::Vec2 center;
};

struct CopiedGroup {
    CopiedKey head;
    std::vector<CopiedKey> members;
};

std::vector<CopiedGroup>& copiedKeys()
{
    static std::vector<CopiedGroup> keys;
    return keys;
}

constexpr double kChildReach = 0.15;
constexpr std::size_t kChildIdRoom = 60;

Json toJson(const QVariant& value)
{
    switch (value.typeId()) {
    case QMetaType::Bool:
        return value.toBool();
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return value.toLongLong();
    case QMetaType::Double:
    case QMetaType::Float:
        return value.toDouble();
    case QMetaType::QString:
        return value.toString().toStdString();
    case QMetaType::QVariantList:
    case QMetaType::QStringList: {
        Json array = Json::array();
        for (const QVariant& item : value.toList()) {
            array.push_back(toJson(item));
        }
        return array;
    }
    case QMetaType::QVariantMap: {
        Json object = Json::object();
        const QVariantMap map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it) {
            object[it.key().toStdString()] = toJson(it.value());
        }
        return object;
    }
    default:
        return nullptr;
    }
}

QVariant toVariant(const Json& value)
{
    if (value.is_boolean()) {
        return value.get<bool>();
    }
    if (value.is_number_integer()) {
        return QVariant::fromValue(value.get<qint64>());
    }
    if (value.is_number()) {
        return value.get<double>();
    }
    if (value.is_string()) {
        return QString::fromStdString(value.get<std::string>());
    }
    if (value.is_array()) {
        QVariantList list;
        for (const Json& item : value) {
            list.append(toVariant(item));
        }
        return list;
    }
    if (value.is_object()) {
        QVariantMap map;
        for (auto it = value.begin(); it != value.end(); ++it) {
            map.insert(QString::fromStdString(it.key()), toVariant(it.value()));
        }
        return map;
    }
    return {};
}

std::vector<std::string> segmentsOf(const QString& path)
{
    std::vector<std::string> segments;
    for (const QString& part : path.split(u'.', Qt::SkipEmptyParts)) {
        segments.push_back(part.toStdString());
    }
    return segments;
}

std::optional<std::size_t> arrayIndex(const std::string& segment)
{
    std::size_t index = 0;
    const auto [end, error] = std::from_chars(segment.data(), segment.data() + segment.size(), index);
    if (error != std::errc{} || end != segment.data() + segment.size()) {
        return std::nullopt;
    }
    return index;
}

const Json* find(const Json& root, const std::vector<std::string>& segments)
{
    const Json* node = &root;
    for (const std::string& segment : segments) {
        if (node->is_array()) {
            const auto index = arrayIndex(segment);
            if (!index || *index >= node->size()) {
                return nullptr;
            }
            node = &(*node)[*index];
        } else if (node->is_object() && node->contains(segment)) {
            node = &(*node)[segment];
        } else {
            return nullptr;
        }
    }
    return node;
}

void open(Json& node)
{
    if (node.is_string()) {
        node = Json{{"input", node}};
    } else if (!node.is_object()) {
        node = Json::object();
    }
}

bool assign(Json& root, const std::vector<std::string>& segments, Json value)
{
    if (segments.empty()) {
        return false;
    }
    Json* node = &root;
    for (std::size_t i = 0; i + 1 < segments.size(); ++i) {
        if (node->is_array()) {
            const auto index = arrayIndex(segments[i]);
            if (!index || *index >= node->size()) {
                return false;
            }
            node = &(*node)[*index];
            continue;
        }
        open(*node);
        node = &(*node)[segments[i]];
    }
    const std::string& last = segments.back();
    if (node->is_array()) {
        const auto index = arrayIndex(last);
        if (!index || *index >= node->size()) {
            return false;
        }
        (*node)[*index] = std::move(value);
        return true;
    }
    open(*node);
    if (value.is_null()) {
        node->erase(last);
    } else {
        (*node)[last] = std::move(value);
    }
    return true;
}

std::optional<input::ControlKind> kindFor(std::string_view type)
{
    const auto found = std::ranges::find(input::kControlTypeNames, type);
    if (found == input::kControlTypeNames.end() || type == "component" || type == "behavior") {
        return std::nullopt;
    }
    if (type == "swipe") {
        input::SwipeControl swipe;
        swipe.path = {{0.0, 0.0}, {0.2, 0.0}};
        return swipe;
    }
    if (type == "skill") {
        input::SkillControl skill;
        skill.direction = input::AimDirection::Cursor;
        return skill;
    }
    input::ControlKind kind;
    const auto index = static_cast<std::size_t>(std::distance(input::kControlTypeNames.begin(), found));
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        static_cast<void>(((index == I ? (kind.emplace<I>(), true) : false) || ...));
    }(std::make_index_sequence<std::variant_size_v<input::ControlKind>>{});
    return kind;
}

input::Vec2 clamped(input::Vec2 point)
{
    return {std::clamp(point.x, 0.0, 1.0), std::clamp(point.y, 0.0, 1.0)};
}

double fine(double value)
{
    return std::round(value * 1e6) / 1e6;
}

const ComponentEntry* classOf(const input::Control& control)
{
    const std::string& ref = input::componentRef(control);
    const ComponentLibrary* library = ComponentLibrary::instance();
    return !ref.empty() && library != nullptr ? library->find(QString::fromStdString(ref)) : nullptr;
}

std::vector<input::NamedBinding>& keysOf(input::Control& control)
{
    auto* placed = std::get_if<input::ComponentControl>(&control.kind);
    return placed != nullptr ? placed->keys : control.keys;
}

std::vector<input::NamedValue>& propsOf(input::Control& control)
{
    auto* placed = std::get_if<input::ComponentControl>(&control.kind);
    return placed != nullptr ? placed->props : control.props;
}

void inDeclaredOrder(input::Profile& profile)
{
    for (input::Control& control : profile.controls) {
        const ComponentEntry* entry = classOf(control);
        if (entry == nullptr) {
            continue;
        }
        const auto rank = [entry](const std::string& name) {
            const QString wanted = QString::fromStdString(name);
            return std::ranges::distance(entry->props.begin(), std::ranges::find(entry->props, wanted, &DeclaredSpec::name));
        };
        std::ranges::stable_sort(keysOf(control), {}, [&rank](const input::NamedBinding& key) { return rank(key.name); });
        std::ranges::stable_sort(propsOf(control), {}, [&rank](const input::NamedValue& value) { return rank(value.name); });
    }
}

bool samePlace(const input::Control& a, const input::Control& b)
{
    return a.anchor == b.anchor && a.offset == b.offset;
}

std::string childId(const std::string& group, const QString& child, const input::Profile& profile)
{
    std::string base = group + "-";
    for (const QChar c : child) {
        if ((c.isLetterOrNumber() && c.unicode() < 0x80) || c == u'_') {
            base += static_cast<char>(c.unicode());
        } else if (!base.ends_with('-')) {
            base += '-';
        }
    }
    while (base.ends_with('-')) {
        base.pop_back();
    }
    base = base.substr(0, kChildIdRoom);
    const auto taken = [&](const std::string& id) {
        return std::ranges::any_of(profile.controls, [&](const input::Control& c) { return c.id == id; }) ||
               std::ranges::any_of(profile.vars, [&](const input::Variable& v) { return v.id == id; });
    };
    if (!taken(base)) {
        return base;
    }
    for (int number = 2;; ++number) {
        std::string candidate = base + "-" + std::to_string(number);
        if (!taken(candidate)) {
            return candidate;
        }
    }
}

input::Vec2 aroundGroup(const input::CoordinateMapper& map, input::Vec2 center, std::size_t index, std::size_t count)
{
    const double angle = -std::numbers::pi / 2.0 +
                         2.0 * std::numbers::pi * static_cast<double>(index) / static_cast<double>(std::max<std::size_t>(count, 1));
    const input::Vec2 step = map.extent({kChildReach * std::cos(angle), kChildReach * std::sin(angle)});
    return {center.x + step.x, center.y + step.y};
}

QVariantList pairOf(input::Vec2 value)
{
    return {fine(value.x), fine(value.y)};
}

const QVariantMap kUnboundKey{{QStringLiteral("slot"), QString()},
                              {QStringLiteral("text"), QStringLiteral("?")},
                              {QStringLiteral("mouse"), false}};

void forget(input::Profile& profile, const std::string& id)
{
    for (input::Control& control : profile.controls) {
        if (auto* skill = std::get_if<input::SkillControl>(&control.kind); skill != nullptr && skill->pauses == id) {
            skill->pauses.clear();
        }
    }
}

std::optional<input::Profile> withValue(const input::Profile& profile, std::size_t index, const QString& path,
                                        const QVariant& value)
{
    Json root = Json::parse(input::saveProfile(profile), nullptr, false);
    if (root.is_discarded() || !root.contains("controls") || index >= root["controls"].size()) {
        return std::nullopt;
    }
    if (!assign(root["controls"][index], segmentsOf(path), toJson(value))) {
        return std::nullopt;
    }
    input::LoadResult loaded = input::loadProfile(root.dump());
    if (!loaded.ok()) {
        return std::nullopt;
    }
    return std::move(loaded.profile);
}

bool clashes(const input::Profile& profile, std::size_t index, const QString& path)
{
    const std::string where = "$.controls[" + std::to_string(index) + "]." + path.toStdString();
    return std::ranges::any_of(input::inputConflicts(profile), [&](const input::InputConflict& conflict) {
        return std::ranges::any_of(conflict.bindings,
                                   [&](const input::ConflictingBinding& binding) { return binding.path == where; });
    });
}

using Defaults = std::vector<std::pair<QString, QString>>;

void giveDefaults(input::Profile& profile, std::size_t index, const Defaults& defaults)
{
    for (const auto& [slot, name] : defaults) {
        const QString path = QStringLiteral("inputs.") + slot;
        std::optional<input::Profile> candidate = withValue(profile, index, path, name);
        if (candidate && !clashes(*candidate, index, path)) {
            profile = std::move(*candidate);
        }
    }
}

const Defaults& moveKeys()
{
    static const Defaults keys{{QStringLiteral("up"), QStringLiteral("KeyW")},
                               {QStringLiteral("down"), QStringLiteral("KeyS")},
                               {QStringLiteral("left"), QStringLiteral("KeyA")},
                               {QStringLiteral("right"), QStringLiteral("KeyD")}};
    return keys;
}

QString nameOf(std::string_view name)
{
    return QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()));
}

qsizetype pointCount(const QString& text)
{
    return text.toUcs4().size();
}

QString firstPoints(const QString& text, qsizetype count)
{
    qsizetype points = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (points == count) {
            return text.left(i);
        }
        if (text[i].isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate()) {
            ++i;
        }
        ++points;
    }
    return text;
}

QString stemOf(const QString& name)
{
    const qsizetype space = name.lastIndexOf(u' ');
    if (space <= 0 || space + 1 >= name.size()) {
        return name;
    }
    for (qsizetype i = space + 1; i < name.size(); ++i) {
        if (!name[i].isDigit()) {
            return name;
        }
    }
    return name.left(space);
}

bool nameTaken(const input::Profile& profile, const std::string& name)
{
    return std::ranges::any_of(profile.controls, [&](const input::Control& c) { return c.name == name; });
}

std::string numberedName(const QString& base, const input::Profile& profile, bool bare)
{
    if (base.isEmpty()) {
        return {};
    }
    const auto fitted = [&](const QString& suffix) {
        const qsizetype room = static_cast<qsizetype>(input::kNameLength) - pointCount(suffix);
        return (firstPoints(base, std::max<qsizetype>(room, 0)) + suffix).toStdString();
    };
    if (bare) {
        std::string plain = fitted({});
        if (!nameTaken(profile, plain)) {
            return plain;
        }
    }
    for (int number = 2;; ++number) {
        std::string candidate = fitted(QStringLiteral(" %1").arg(number));
        if (!nameTaken(profile, candidate)) {
            return candidate;
        }
    }
}

std::uint8_t modifierBits(int modifiers)
{
    std::uint8_t bits = 0;
    if ((modifiers & Qt::ControlModifier) != 0) {
        bits = static_cast<std::uint8_t>(bits | input::modifier::kCtrl);
    }
    if ((modifiers & Qt::ShiftModifier) != 0) {
        bits = static_cast<std::uint8_t>(bits | input::modifier::kShift);
    }
    if ((modifiers & Qt::AltModifier) != 0) {
        bits = static_cast<std::uint8_t>(bits | input::modifier::kAlt);
    }
    return bits;
}

QString chordText(std::string_view source, int modifiers)
{
    const std::optional<input::InputSource> parsed = input::parseSource(source);
    if (!parsed) {
        return {};
    }
    return QString::fromStdString(input::chordName({.modifiers = modifierBits(modifiers), .source = *parsed}));
}

QVariantList capsOf(const std::optional<input::Chord>& chord)
{
    QVariantList caps;
    if (!chord) {
        return caps;
    }
    for (const KeyLabel& cap : keyCaps(*chord)) {
        caps.append(QVariantMap{{QStringLiteral("text"), cap.text}, {QStringLiteral("mouse"), cap.mouse}});
    }
    return caps;
}

bool hasField(const input::Profile& profile, std::size_t index, const QString& path)
{
    const Json root = Json::parse(input::saveProfile(profile), nullptr, false);
    return !root.is_discarded() && root.contains("controls") && index < root["controls"].size() &&
           find(root["controls"][index], segmentsOf(path)) != nullptr;
}

std::optional<input::Profile> withInput(const input::Profile& profile, std::size_t index, const QString& binding,
                                        const QString& chord)
{
    std::optional<input::Profile> next = profile;
    const QString claim = binding + QStringLiteral(".claim");
    if (!input::claimable(input::parseChord(chord.toStdString())) && hasField(profile, index, claim)) {
        next = withValue(profile, index, claim, QVariant());
    }
    if (next) {
        next = withValue(*next, index, binding + QStringLiteral(".input"), chord);
    }
    return next;
}

std::vector<std::string_view> keySlotsOf(const input::ControlKind& kind)
{
    if (const auto* placed = std::get_if<input::ComponentControl>(&kind)) {
        std::vector<std::string_view> names;
        for (const input::NamedBinding& key : placed->keys) {
            names.push_back(key.name);
        }
        return names;
    }
    if (const auto* behavior = std::get_if<input::BehaviorControl>(&kind)) {
        std::vector<std::string_view> names;
        for (const input::NamedBinding& entry : behavior->inputs) {
            if (entry.binding && entry.binding->input &&
                input::kindOf(entry.binding->input->source) != input::SourceKind::Axis2D) {
                names.push_back(entry.name);
            }
        }
        return names;
    }
    if (std::holds_alternative<input::WasdControl>(kind)) {
        return {"up", "down", "left", "right", "walk", "sprint"};
    }
    if (std::holds_alternative<input::RightWalkControl>(kind)) {
        return {"walkTo", "stop"};
    }
    if (std::holds_alternative<input::PadStickControl>(kind) || std::holds_alternative<input::PadCameraControl>(kind)) {
        return {};
    }
    if (std::holds_alternative<input::CameraControl>(kind)) {
        return {"lockKey"};
    }
    if (std::holds_alternative<input::FreeLookControl>(kind)) {
        return {"hold"};
    }
    if (std::holds_alternative<input::SkillControl>(kind)) {
        return {"press", "cast", "cancel"};
    }
    if (std::holds_alternative<input::PinchControl>(kind)) {
        return {"zoomIn", "zoomOut"};
    }
    return {"press"};
}

std::vector<std::string_view> primarySlotsOf(const input::ControlKind& kind)
{
    if (std::holds_alternative<input::WasdControl>(kind)) {
        return {"up", "down", "left", "right"};
    }
    if (std::holds_alternative<input::RightWalkControl>(kind)) {
        return {"walkTo"};
    }
    if (std::holds_alternative<input::SkillControl>(kind)) {
        return {"press"};
    }
    return keySlotsOf(kind);
}

QVariantList withPlaceholders(const QVariantList& keys, const std::vector<std::string_view>& primary)
{
    const auto slotOf = [](const QVariant& key) { return key.toMap().value(QStringLiteral("slot")).toString(); };
    const auto isPrimary = [&](const QString& slot) {
        return std::ranges::any_of(primary, [&](std::string_view name) { return nameOf(name) == slot; });
    };
    QVariantList ordered;
    for (const std::string_view slot : primary) {
        const QString name = nameOf(slot);
        const auto found = std::ranges::find_if(keys, [&](const QVariant& key) { return slotOf(key) == name; });
        if (found != keys.end()) {
            ordered.append(*found);
            continue;
        }
        QVariantMap placeholder = kUnboundKey;
        placeholder.insert(QStringLiteral("slot"), name);
        ordered.append(placeholder);
    }
    for (const QVariant& key : keys) {
        if (!isPrimary(slotOf(key))) {
            ordered.append(key);
        }
    }
    return ordered;
}

std::optional<Json> entryOf(const input::Profile& profile, std::size_t index)
{
    Json root = Json::parse(input::saveProfile(profile), nullptr, false);
    if (root.is_discarded() || !root.contains("controls") || index >= root["controls"].size()) {
        return std::nullopt;
    }
    return root["controls"][index];
}

std::optional<std::string> walkerOf(const input::Profile& profile)
{
    const auto walking = std::ranges::find_if(profile.controls, [](const input::Control& control) {
        return std::holds_alternative<input::RightWalkControl>(control.kind);
    });
    if (walking != profile.controls.end()) {
        return walking->id;
    }
    const auto moving = std::ranges::find_if(profile.controls,
                                             [](const input::Control& control) { return input::isMovement(control.kind); });
    return moving != profile.controls.end() ? std::optional<std::string>(moving->id) : std::nullopt;
}

bool bound(const Json* binding)
{
    return binding != nullptr && (binding->is_string() || (binding->is_object() && binding->contains("input")));
}

std::string textOf(const Json& object, const char* key)
{
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
}

input::Profile withoutInput(const input::Profile& profile, std::size_t index, const QString& path)
{
    if (path != u"lockKey") {
        std::optional<input::Profile> kept = withValue(profile, index, path + QStringLiteral(".claim"), QVariant());
        if (kept) {
            kept = withValue(*kept, index, path + QStringLiteral(".input"), QVariant());
        }
        if (kept) {
            return std::move(*kept);
        }
    }
    std::optional<input::Profile> dropped = withValue(profile, index, path, QVariant());
    return dropped ? std::move(*dropped) : profile;
}

void adoptSaved(input::Profile& profile, std::size_t index, const QString& kind, QString& slot)
{
    const ComponentDefaults* defaults = ComponentDefaults::instance();
    const std::optional<Json> saved = defaults != nullptr ? defaults->find(kind.toStdString()) : std::nullopt;
    if (!saved || !saved->is_object()) {
        return;
    }
    Json root = Json::parse(input::saveProfile(profile), nullptr, false);
    if (root.is_discarded() || !root.contains("controls") || index >= root["controls"].size()) {
        return;
    }
    const Json made = root["controls"][index];
    if (textOf(*saved, "type") != textOf(made, "type")) {
        return;
    }
    Json control = *saved;
    for (const char* key : {"id", "name", "anchor", "offset"}) {
        if (made.contains(key)) {
            control[key] = made[key];
        } else {
            control.erase(key);
        }
    }
    control.erase("enabled");
    control.erase("when");
    if (control.contains("pauses")) {
        if (const std::optional<std::string> walker = walkerOf(profile)) {
            control["pauses"] = *walker;
        } else {
            control.erase("pauses");
        }
    }
    root["controls"][index] = control;
    input::LoadResult loaded = input::loadProfile(root.dump());
    if (!loaded.ok()) {
        return;
    }
    input::Profile adopted = std::move(loaded.profile);
    QStringList keys;
    if (const auto inputs = control.find("inputs"); inputs != control.end() && inputs->is_object()) {
        for (auto it = inputs->begin(); it != inputs->end(); ++it) {
            keys.append(QStringLiteral("inputs.") + QString::fromStdString(it.key()));
        }
    }
    if (control.contains("lockKey")) {
        keys.append(QStringLiteral("lockKey"));
    }
    for (const QString& key : keys) {
        if (clashes(adopted, index, key)) {
            adopted = withoutInput(adopted, index, key);
        }
    }
    profile = std::move(adopted);
    if (slot.isEmpty()) {
        return;
    }
    const std::optional<Json> now = entryOf(profile, index);
    if (now && bound(find(*now, segmentsOf(slot)))) {
        slot.clear();
    }
}

struct Recipe {
    std::string_view kind;
    std::string_view base;
    const char* slot = "";
    Defaults defaults;
};

const std::vector<Recipe>& recipes()
{
    static const std::vector<Recipe> all{
        {"tap", "tap", "inputs.press", {}},
        {"hold", "hold", "inputs.press", {}},
        {"longPress", "long", "inputs.press", {}},
        {"turbo", "turbo", "inputs.press", {}},
        {"swipe", "swipe", "inputs.press", {}},
        {"pinch",
         "zoom",
         "",
         {{QStringLiteral("zoomIn"), QStringLiteral("Mouse.WheelUp")},
          {QStringLiteral("zoomOut"), QStringLiteral("Mouse.WheelDown")}}},
        {"wasd", "move", "", moveKeys()},
        {"rightWalk", "walk", "", {{QStringLiteral("walkTo"), QStringLiteral("Mouse.Right")}}},
        {"padStick", "stick", "", {{QStringLiteral("stick"), QStringLiteral("Gamepad.LeftStick")}}},
        {"camera", "look", "", {{QStringLiteral("look"), QStringLiteral("Mouse.Move")}}},
        {"padCamera", "padLook", "", {{QStringLiteral("look"), QStringLiteral("Gamepad.RightStick")}}},
        {"freeLook",
         "peek",
         "",
         {{QStringLiteral("hold"), QStringLiteral("AltLeft")}, {QStringLiteral("look"), QStringLiteral("Mouse.Move")}}},
        {"skill", "skill", "inputs.press", {{QStringLiteral("cast"), QStringLiteral("Mouse.Left")}}},
        {"dial", "dial", "inputs.press", {{QStringLiteral("look"), QStringLiteral("Mouse.Move")}}},
        {"multiPress", "multi", "inputs.press", {}},
        {"tempCursor", "cursor", "inputs.press", {}},
        {"actionWheel", "wheel", "inputs.press", {{QStringLiteral("look"), QStringLiteral("Mouse.Move")}}},
    };
    return all;
}

QString upperFirst(QString text)
{
    if (!text.isEmpty()) {
        text[0] = text[0].toUpper();
    }
    return text;
}

QString sourcePiece(std::string_view name)
{
    if (name.size() == 4 && name.starts_with("Key")) {
        return QString(QChar(QLatin1Char(name[3])));
    }
    if (name.size() == 6 && name.starts_with("Digit")) {
        return QString(QChar(QLatin1Char(name[5])));
    }
    QString piece;
    for (const char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
            piece += QLatin1Char(c);
        }
    }
    return upperFirst(piece);
}

QString keyPiece(std::string_view chord)
{
    if (chord.empty()) {
        return {};
    }
    QString piece;
    std::string_view rest = chord;
    while (true) {
        const std::size_t plus = rest.find('+');
        if (plus == std::string_view::npos) {
            break;
        }
        piece += upperFirst(QString::fromLatin1(rest.substr(0, plus).data(), static_cast<qsizetype>(plus)));
        rest.remove_prefix(plus + 1);
    }
    piece += sourcePiece(rest);
    return piece;
}

std::string stemOfId(const std::string& id)
{
    std::string stem = id;
    while (!stem.empty() && stem.back() >= '0' && stem.back() <= '9') {
        stem.pop_back();
    }
    return stem;
}

bool codeNameWord(std::string_view word)
{
    return !word.empty() && word.size() <= input::kNameLength &&
           std::ranges::all_of(word, [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                      c == '-';
           });
}

bool codeNameReserved(std::string_view word)
{
    static constexpr auto kReserved = std::to_array<std::string_view>(
        {"await", "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do",
         "else", "enum", "export", "extends", "false", "finally", "for", "function", "if", "import", "in",
         "instanceof", "let", "new", "null", "return", "static", "super", "switch", "this", "throw", "true", "try",
         "typeof", "var", "void", "while", "with", "yield", "controls", "keys", "vars", "props", "this"});
    return std::ranges::find(kReserved, word) != kReserved.end();
}

const input::Control* controlById(const input::Profile& profile, const std::string& id)
{
    const auto found = std::ranges::find(profile.controls, id, &input::Control::id);
    return found != profile.controls.end() ? &*found : nullptr;
}

std::optional<std::string> chordIn(const Json& settings, std::string_view slot, bool topLevel)
{
    if (topLevel) {
        const auto found = settings.find(std::string(slot));
        if (found == settings.end()) {
            return std::nullopt;
        }
        if (found->is_string()) {
            return found->get<std::string>();
        }
        if (found->is_object()) {
            const auto input = found->find("input");
            if (input != found->end() && input->is_string()) {
                return input->get<std::string>();
            }
        }
        return std::nullopt;
    }
    const auto group = settings.find("inputs");
    if (group == settings.end() || !group->is_object()) {
        return std::nullopt;
    }
    const auto found = group->find(std::string(slot));
    if (found == group->end()) {
        return std::nullopt;
    }
    if (found->is_string()) {
        return found->get<std::string>();
    }
    if (found->is_object()) {
        const auto input = found->find("input");
        if (input != found->end() && input->is_string()) {
            return input->get<std::string>();
        }
    }
    return std::nullopt;
}

}

ProfileEditor::ProfileEditor(input::Profile profile, input::ScreenGeometry screen, QObject* parent)
    : QObject(parent), m_saved(profile), m_screen(screen), m_defaultLockKey(input::parseChord("Backquote"))
{
    m_history.push_back({.profile = std::move(profile)});
    input::Profile synced = this->profile();
    if (syncChildren(synced)) {
        m_history.push_back({.profile = std::move(synced)});
        m_at = 1;
    }
    m_conflicts = input::inputConflicts(this->profile());
}

QString ProfileEditor::profileName() const
{
    return QString::fromStdString(profile().meta.name);
}

bool ProfileEditor::dirty() const
{
    return !(profile() == m_saved);
}

QStringList ProfileEditor::conflictingIds() const
{
    QStringList ids;
    for (const input::InputConflict& conflict : m_conflicts) {
        for (const input::ConflictingBinding& binding : conflict.bindings) {
            std::string owner;
            if (binding.index < profile().controls.size()) {
                owner = profile().controls[binding.index].id;
            }
            const QString id = QString::fromStdString(owner);
            if (!id.isEmpty() && !ids.contains(id)) {
                ids.append(id);
            }
        }
    }
    return ids;
}

QHash<QString, int> ProfileEditor::conflictGroups() const
{
    QHash<QString, int> groups;
    for (std::size_t group = 0; group < m_conflicts.size(); ++group) {
        for (const input::ConflictingBinding& binding : m_conflicts[group].bindings) {
            if (binding.index >= profile().controls.size()) {
                continue;
            }
            const QString id = QString::fromStdString(profile().controls[binding.index].id);
            if (!groups.contains(id)) {
                groups.insert(id, static_cast<int>(group));
            }
        }
    }
    return groups;
}

void ProfileEditor::setScreen(const input::ScreenGeometry& screen)
{
    m_screen = screen;
    ++m_revision;
    emit changed();
}

void ProfileEditor::markSaved()
{
    m_saved = profile();
    ++m_revision;
    emit changed();
}

QVariantList ProfileEditor::markers(double width, double height) const
{
    QVariantList items = keyHintItems(profile(), QSizeF(width, height), m_defaultLockKey, true);
    const QHash<QString, int> groups = conflictGroups();
    const input::CoordinateMapper geometry = mapper();
    for (QVariant& item : items) {
        QVariantMap map = item.toMap();
        const auto index = static_cast<std::size_t>(map.value(QStringLiteral("control")).toInt());
        if (index >= profile().controls.size()) {
            continue;
        }
        const input::Control& control = profile().controls[index];
        const QString id = QString::fromStdString(control.id);
        const QVariantList bound = map.value(QStringLiteral("keys")).toList();
        const bool unbound = bound.isEmpty();
        map.insert(QStringLiteral("keys"), withPlaceholders(bound, primarySlotsOf(control.kind)));
        map.insert(QStringLiteral("id"), id);
        map.insert(QStringLiteral("name"), displayName(index));
        map.insert(QStringLiteral("unbound"), unbound);
        map.insert(QStringLiteral("live"), true);
        map.insert(QStringLiteral("dim"), !control.enabled);
        map.insert(QStringLiteral("selected"), m_selection.contains(id));
        map.insert(QStringLiteral("conflict"), groups.contains(id));
        map.insert(QStringLiteral("conflictGroup"), groups.value(id, -1));
        map.insert(QStringLiteral("logicKey"), m_handedOver.contains(id) && !unbound);
        map.insert(QStringLiteral("enabled"), control.enabled);
        map.insert(QStringLiteral("parent"), QString::fromStdString(control.parent));
        map.insert(QStringLiteral("diff"), m_previewMarks.value(id));
        if (const QVariantList extra = modulePoints(index, width, height); !extra.isEmpty()) {
            map.insert(QStringLiteral("points"), map.value(QStringLiteral("points")).toList() + extra);
            map.insert(QStringLiteral("modulePoints"), true);
        }
        if (const auto* swipe = std::get_if<input::SwipeControl>(&control.kind)) {
            const input::Vec2 origin = geometry.position(control.anchor, control.offset);
            QVariantList points;
            for (const input::Vec2& point : swipe->path) {
                const input::Vec2 extent = geometry.extent(point);
                points.append(QVariantMap{{QStringLiteral("x"), (origin.x + extent.x) * width},
                                          {QStringLiteral("y"), (origin.y + extent.y) * height}});
            }
            map.insert(QStringLiteral("path"), points);
        }
        item = map;
    }
    if (m_preview) {
        const QVariantList live = keyHintItems(m_preview->live, QSizeF(width, height), m_defaultLockKey, true);
        for (const QVariant& item : live) {
            QVariantMap map = item.toMap();
            const auto index = static_cast<std::size_t>(map.value(QStringLiteral("control")).toInt());
            if (index >= m_preview->live.controls.size()) {
                continue;
            }
            const input::Control& control = m_preview->live.controls[index];
            const QString id = QString::fromStdString(control.id);
            if (m_previewMarks.value(id) != u"removed") {
                continue;
            }
            const QVariantList bound = map.value(QStringLiteral("keys")).toList();
            map.insert(QStringLiteral("keys"), withPlaceholders(bound, primarySlotsOf(control.kind)));
            map.insert(QStringLiteral("id"), id);
            map.insert(QStringLiteral("name"),
                       control.name.empty() ? id : QString::fromStdString(control.name));
            map.insert(QStringLiteral("unbound"), bound.isEmpty());
            map.insert(QStringLiteral("live"), true);
            map.insert(QStringLiteral("dim"), !control.enabled);
            map.insert(QStringLiteral("selected"), false);
            map.insert(QStringLiteral("conflict"), false);
            map.insert(QStringLiteral("conflictGroup"), -1);
            map.insert(QStringLiteral("logicKey"), false);
            map.insert(QStringLiteral("enabled"), control.enabled);
            map.insert(QStringLiteral("parent"), QString::fromStdString(control.parent));
            map.insert(QStringLiteral("diff"), QStringLiteral("removed"));
            map.insert(QStringLiteral("ghost"), true);
            items.append(map);
        }
    }
    return items;
}

QVariantList ProfileEditor::keyList() const
{
    const QStringList conflicting = conflictingIds();
    const QHash<QString, int> groups = conflictGroups();
    QVariantList first;
    QVariantList rest;
    const QVariantList items = keyHintItems(profile(), QSizeF(1.0, 1.0), m_defaultLockKey, true);
    for (const QVariant& item : items) {
        const QVariantMap map = item.toMap();
        const auto index = static_cast<std::size_t>(map.value(QStringLiteral("control")).toInt());
        if (index >= profile().controls.size()) {
            continue;
        }
        const input::Control& control = profile().controls[index];
        QStringList keys;
        for (const QVariant& key : map.value(QStringLiteral("keys")).toList()) {
            keys.append(key.toMap().value(QStringLiteral("text")).toString());
        }
        const QString id = QString::fromStdString(control.id);
        QVariantMap row{{QStringLiteral("id"), id},
                        {QStringLiteral("name"), displayName(index)},
                        {QStringLiteral("ordinal"), static_cast<int>(index + 1)},
                        {QStringLiteral("type"), map.value(QStringLiteral("type"))},
                        {QStringLiteral("component"), map.value(QStringLiteral("component"))},
                        {QStringLiteral("keys"), keys.join(u' ')},
                        {QStringLiteral("enabled"), control.enabled},
                        {QStringLiteral("selected"), m_selection.contains(id)},
                        {QStringLiteral("conflict"), conflicting.contains(id)},
                        {QStringLiteral("conflictGroup"), groups.value(id, -1)},
                        {QStringLiteral("logicKey"), m_handedOver.contains(id) && !keys.isEmpty()},
                        {QStringLiteral("parent"), QString::fromStdString(control.parent)},
                        {QStringLiteral("diff"), m_previewMarks.value(id)},
                        {QStringLiteral("depth"), 0}};
        if (const auto* behavior = std::get_if<input::BehaviorControl>(&control.kind)) {
            row.insert(QStringLiteral("behavior"), QString::fromStdString(behavior->behavior));
        }
        (conflicting.contains(id) ? first : rest).append(row);
    }
    const QVariantList rows = first + rest;
    const auto parentOf = [](const QVariant& row) { return row.toMap().value(QStringLiteral("parent")).toString(); };
    const auto rowOf = [&rows](const QString& id) {
        return std::ranges::find_if(rows, [&](const QVariant& row) {
            return row.toMap().value(QStringLiteral("id")).toString() == id;
        });
    };
    const auto nested = [&](const QVariant& row) {
        const QString parent = parentOf(row);
        const auto group = parent.isEmpty() ? rows.end() : rowOf(parent);
        return group != rows.end() && parentOf(*group).isEmpty();
    };
    QVariantList ordered;
    for (const QVariant& row : rows) {
        if (nested(row)) {
            continue;
        }
        ordered.append(row);
        const QString id = row.toMap().value(QStringLiteral("id")).toString();
        for (const QVariant& child : rows) {
            if (parentOf(child) == id && nested(child)) {
                QVariantMap indented = child.toMap();
                indented.insert(QStringLiteral("depth"), 1);
                ordered.append(indented);
            }
        }
    }
    return ordered;
}

QString ProfileEditor::addControl(const QString& type, double x, double y)
{
    const auto kind = kindFor(type.toStdString());
    if (!kind) {
        return {};
    }
    input::Profile next = profile();
    input::Control control;
    control.id = freeId(type.toStdString(), next);
    control.name = numberedName(nameFor(type), next, true);
    control.kind = *kind;
    if (auto* camera = std::get_if<input::CameraControl>(&control.kind)) {
        camera->lockKey = m_defaultLockKey;
    }
    placeAt(control, {x, y});
    const QString id = QString::fromStdString(control.id);
    next.controls.push_back(std::move(control));
    commit(std::move(next), QStringList{id});
    return id;
}

QVariantMap ProfileEditor::addComponent(const QString& kind, double x, double y)
{
    if (kind.startsWith(u"component:")) {
        return addPlaced(kind.mid(10), x, y);
    }
    if (kind.startsWith(u"behavior:")) {
        return addBehavior(kind.mid(9), x, y);
    }
    const std::string wanted = kind.toStdString();
    const auto recipe = std::ranges::find(recipes(), wanted, &Recipe::kind);
    if (recipe == recipes().end()) {
        return {};
    }
    input::Profile next = profile();
    input::Control control;
    control.id = freeId(recipe->base, next);
    control.name = numberedName(nameFor(kind), next, true);
    control.kind = *kindFor(recipe->kind);
    if (auto* camera = std::get_if<input::CameraControl>(&control.kind)) {
        camera->lockKey = m_defaultLockKey;
    }
    placeAt(control, {x, y});
    next.controls.push_back(std::move(control));
    giveDefaults(next, next.controls.size() - 1, recipe->defaults);
    QString slot = QString::fromLatin1(recipe->slot);
    adoptSaved(next, next.controls.size() - 1, kind, slot);
    input::Control& made = next.controls.back();
    const std::string part = keyPartOf(made);
    if (!part.empty()) {
        made.id = uniqueId(std::string(recipe->base) + part, next, made.id);
    }
    const QString id = QString::fromStdString(made.id);
    commit(std::move(next), QStringList{id});
    return {{QStringLiteral("id"), id}, {QStringLiteral("slot"), slot}};
}

bool ProfileEditor::bindInput(const QString& id, const QString& slot, const QString& input)
{
    const auto at = indexOf(id);
    if (!at || !slot.startsWith(QStringLiteral("inputs."))) {
        return setValue(id, slot, input);
    }
    std::optional<input::Profile> next = withInput(profile(), *at, slot, input);
    if (!next) {
        return false;
    }
    commit(std::move(*next));
    return true;
}

QString ProfileEditor::inputOfKey(quint32 nativeScanCode, int modifiers) const
{
    constexpr quint32 kExtended = 0xE000;
    input::KeyboardDecoder decoder;
    const std::optional<input::DeviceEvent> event = decoder.decode(
        static_cast<std::uint16_t>(nativeScanCode & 0xFF), (nativeScanCode & kExtended) != 0, false, false);
    if (!event) {
        return {};
    }
    input::Chord chord{.source = event->source};
    if (!input::isModifierKey(event->source)) {
        chord.modifiers = modifierBits(modifiers);
    }
    return QString::fromStdString(input::chordName(chord));
}

QString ProfileEditor::inputOfMouse(int button, int modifiers) const
{
    switch (button) {
    case Qt::LeftButton:
        return chordText("Mouse.Left", modifiers);
    case Qt::RightButton:
        return chordText("Mouse.Right", modifiers);
    case Qt::MiddleButton:
        return chordText("Mouse.Middle", modifiers);
    case Qt::BackButton:
        return chordText("Mouse.X1", modifiers);
    case Qt::ForwardButton:
        return chordText("Mouse.X2", modifiers);
    default:
        return {};
    }
}

QString ProfileEditor::inputOfWheel(int dx, int dy, int modifiers) const
{
    if (dx == 0 && dy == 0) {
        return {};
    }
    if (std::abs(dy) >= std::abs(dx)) {
        return chordText(dy > 0 ? "Mouse.WheelUp" : "Mouse.WheelDown", modifiers);
    }
    return chordText(dx > 0 ? "Mouse.WheelLeft" : "Mouse.WheelRight", modifiers);
}

bool ProfileEditor::rename(const QString& id, const QString& name)
{
    const QString cleaned = name.simplified();
    const auto at = indexOf(id);
    if (!at || cleaned.isEmpty() || pointCount(cleaned) > static_cast<qsizetype>(input::kNameLength)) {
        return false;
    }
    input::Profile next = profile();
    next.controls[*at].name = cleaned.toStdString();
    commit(std::move(next));
    return true;
}

QVariantList ProfileEditor::form(const QString& id) const
{
    const auto at = indexOf(id);
    if (!at) {
        return {};
    }
    const std::string section = "$.controls[" + std::to_string(*at) + "].";
    const form::Context context{.profile = profile(), .index = *at, .mapper = mapper(), .width = m_screen.width};
    QVariantList groups = form::build(context, [&](const form::Field& field, const std::optional<input::Chord>& input) {
        const QString binding = QString::fromStdString(form::bindingOf(field.path));
        const std::string where = section + (binding.isEmpty() ? nameOf(field.path) : binding).toStdString();
        return QVariantMap{{QStringLiteral("key"), nameOf(field.key)},
                           {QStringLiteral("widget"), QStringLiteral("key")},
                           {QStringLiteral("slot"), nameOf(field.key)},
                           {QStringLiteral("title"), form::text(field.title)},
                           {QStringLiteral("hint"), QString()},
                           {QStringLiteral("half"), field.half},
                           {QStringLiteral("enabled"), true},
                           {QStringLiteral("keys"), capsOf(input)},
                           {QStringLiteral("bindPath"), binding},
                           {QStringLiteral("clearPath"), binding},
                           {QStringLiteral("conflict"), input ? conflictText(where) : QString()}};
    });
    if (const QVariantList fields = placedFields(*at); !fields.isEmpty()) {
        groups.prepend(QVariantMap{{QStringLiteral("fields"), fields}});
    }
    if (const QVariantList fields = behaviorFields(*at); !fields.isEmpty()) {
        groups.prepend(QVariantMap{{QStringLiteral("fields"), fields}});
    }
    return groups;
}

bool ProfileEditor::rebind(const QString& id, const QString& slot, const QString& input)
{
    const auto at = indexOf(id);
    if (!at || input.isEmpty()) {
        return false;
    }
    const input::Control& control = profile().controls[*at];
    if (const ComponentEntry* entry = classOf(control)) {
        const bool declared = std::ranges::any_of(entry->props, [&](const DeclaredSpec& spec) {
            return spec.kind == u"key" && spec.name == slot;
        });
        if (declared || std::holds_alternative<input::ComponentControl>(control.kind)) {
            if (!declared) {
                return false;
            }
            std::optional<input::Profile> next = withInput(profile(), *at, QStringLiteral("inputs.") + slot, input);
            if (!next) {
                return false;
            }
            commit(std::move(*next));
            return true;
        }
    }
    const std::vector<std::string_view> names = keySlotsOf(control.kind);
    if (std::ranges::none_of(names, [&](std::string_view name) { return nameOf(name) == slot; })) {
        return false;
    }
    if (slot == u"lockKey") {
        return setValue(id, slot, input);
    }
    std::optional<input::Profile> next = withInput(profile(), *at, QStringLiteral("inputs.") + slot, input);
    if (!next) {
        return false;
    }
    commit(std::move(*next));
    return true;
}

void ProfileEditor::select(const QStringList& ids)
{
    QStringList chosen;
    for (const QString& id : ids) {
        if (indexOf(id) && !chosen.contains(id)) {
            chosen.append(id);
        }
    }
    if (chosen == m_selection) {
        return;
    }
    m_lastEdit.reset();
    m_selection = chosen;
    ++m_revision;
    emit selectionChanged();
    emit changed();
}

void ProfileEditor::toggleSelected(const QString& id)
{
    QStringList next = m_selection;
    if (next.contains(id)) {
        next.removeAll(id);
    } else {
        next.append(id);
    }
    select(next);
}

void ProfileEditor::selectConflicts()
{
    select(conflictingIds());
}

void ProfileEditor::selectAll()
{
    QStringList ids;
    for (const input::Control& control : profile().controls) {
        ids.append(QString::fromStdString(control.id));
    }
    select(ids);
}

void ProfileEditor::beginGesture()
{
    m_inGesture = true;
    m_gestureCommitted = false;
}

void ProfileEditor::endGesture()
{
    m_inGesture = false;
    m_gestureCommitted = false;
}

void ProfileEditor::moveSelection(double dx, double dy)
{
    if (m_selection.isEmpty()) {
        return;
    }
    input::Profile next = profile();
    const input::CoordinateMapper map = mapper();
    for (input::Control& control : next.controls) {
        if (!m_selection.contains(QString::fromStdString(control.id))) {
            continue;
        }
        const input::Vec2 center = map.position(control.anchor, control.offset);
        placeAt(control, {center.x + dx, center.y + dy});
    }
    carryChildren(next);
    commit(std::move(next));
}

void ProfileEditor::placeControl(const QString& id, double x, double y)
{
    const auto index = indexOf(id);
    if (!index) {
        return;
    }
    input::Profile next = profile();
    placeAt(next.controls[*index], {x, y});
    carryChildren(next);
    commit(std::move(next));
}

bool ProfileEditor::setLength(const QString& id, const QString& path, double width)
{
    return setValue(id, path, lengthValue(width));
}

bool ProfileEditor::setArea(const QString& id, const QString& path, double width, double height)
{
    return setValue(id, path, areaValue(width, height));
}

bool ProfileEditor::setPoint(const QString& id, const QString& path, double x, double y)
{
    return setValue(id, path, pointValue(x, y));
}

bool ProfileEditor::setPathPoint(const QString& id, int index, double x, double y)
{
    const auto at = indexOf(id);
    if (!at || index < 0) {
        return false;
    }
    const input::Control& control = profile().controls[*at];
    const input::CoordinateMapper geometry = mapper();
    const input::Vec2 origin = geometry.position(control.anchor, control.offset);
    const input::Vec2 point = clamped({x, y});
    return setValue(id, QStringLiteral("path.%1").arg(index),
                    pairOf(geometry.sizeOf({point.x - origin.x, point.y - origin.y})));
}

void ProfileEditor::alignSelection(const QString& edge)
{
    input::Profile next = profile();
    const auto centers = selectedCenters(next);
    if (centers.size() < 2) {
        return;
    }
    const auto [minX, maxX] = std::ranges::minmax(centers | std::views::transform([](const auto& c) { return c.second.x; }));
    const auto [minY, maxY] = std::ranges::minmax(centers | std::views::transform([](const auto& c) { return c.second.y; }));
    for (const auto& [index, center] : centers) {
        input::Vec2 target = center;
        if (edge == QStringLiteral("left")) {
            target.x = minX;
        } else if (edge == QStringLiteral("centerX")) {
            target.x = (minX + maxX) / 2.0;
        } else if (edge == QStringLiteral("right")) {
            target.x = maxX;
        } else if (edge == QStringLiteral("top")) {
            target.y = minY;
        } else if (edge == QStringLiteral("centerY")) {
            target.y = (minY + maxY) / 2.0;
        } else if (edge == QStringLiteral("bottom")) {
            target.y = maxY;
        } else {
            return;
        }
        placeAt(next.controls[index], target);
    }
    carryChildren(next);
    commit(std::move(next));
}

void ProfileEditor::distributeSelection(bool horizontal)
{
    input::Profile next = profile();
    auto centers = selectedCenters(next);
    if (centers.size() < 3) {
        return;
    }
    const auto along = [horizontal](const input::Vec2& point) { return horizontal ? point.x : point.y; };
    std::ranges::sort(centers, {}, [&](const auto& c) { return along(c.second); });
    const double first = along(centers.front().second);
    const double step = (along(centers.back().second) - first) / static_cast<double>(centers.size() - 1);
    for (std::size_t i = 1; i + 1 < centers.size(); ++i) {
        input::Vec2 target = centers[i].second;
        (horizontal ? target.x : target.y) = first + step * static_cast<double>(i);
        placeAt(next.controls[centers[i].first], target);
    }
    carryChildren(next);
    commit(std::move(next));
}

void ProfileEditor::removeSelection()
{
    if (m_selection.isEmpty()) {
        return;
    }
    const int uses = logicUses();
    if (uses > 0) {
        emit deleteAsked(uses);
        return;
    }
    deleteSelection(false);
}

OpenTexts ProfileEditor::openLogicTexts() const
{
    OpenTexts texts;
    QObject* bench = m_logic.data();
    QVariantMap open;
    if (bench == nullptr
        || !QMetaObject::invokeMethod(bench, "openScriptTexts", Qt::DirectConnection, Q_RETURN_ARG(QVariantMap, open))) {
        return texts;
    }
    for (auto entry = open.cbegin(); entry != open.cend(); ++entry) {
        texts.emplace(entry.key(), entry.value().toString().toUtf8());
    }
    return texts;
}

QStringList ProfileEditor::unsavedLogicFiles() const
{
    QStringList files;
    QObject* bench = m_logic.data();
    if (bench != nullptr
        && !QMetaObject::invokeMethod(bench, "unsavedFiles", Qt::DirectConnection, Q_RETURN_ARG(QStringList, files))) {
        files.clear();
    }
    return files;
}

void ProfileEditor::rememberGone(const QString& id, const QString& name)
{
    QObject* bench = m_logic.data();
    if (bench == nullptr) {
        return;
    }
    QVariantMap gone = bench->property("goneControls").toMap();
    if (gone.value(id).toString() == name) {
        return;
    }
    gone.insert(id, name);
    bench->setProperty("goneControls", gone);
}

void ProfileEditor::restoreLogic(const std::vector<LogicEdit>& edits, bool forward)
{
    if (edits.empty() || m_logicFolder.isEmpty()) {
        return;
    }
    const std::filesystem::path folder(m_logicFolder.toStdWString());
    QStringList moved;
    QStringList failed;
    for (const LogicEdit& edit : edits) {
        const QByteArray& wanted = forward ? edit.after : edit.before;
        const QByteArray& expected = forward ? edit.before : edit.after;
        const bool wantedExists = forward ? edit.existsAfter : edit.existedBefore;
        const bool expectedExists = forward ? edit.existedBefore : edit.existsAfter;
        const std::filesystem::path path = folder / edit.file.toStdWString();
        std::error_code error;
        if (!expectedExists) {
            if (std::filesystem::exists(path, error)) {
                moved.append(edit.file);
                continue;
            }
        } else {
            std::string contents;
            if (platform::readFile(path, kMostLogicFileBytes, contents, platform::WhenBusy::GiveUp)) {
                failed.append(edit.file);
                continue;
            }
            if (QByteArray::fromStdString(contents) != expected) {
                moved.append(edit.file);
                continue;
            }
        }
        if (!wantedExists) {
            if (platform::removeFile(path)) {
                failed.append(edit.file);
            }
            continue;
        }
        std::filesystem::create_directories(path.parent_path(), error);
        if (platform::writeFileAtomically(path, wanted.toStdString())) {
            failed.append(edit.file);
        }
    }
    if (!moved.isEmpty()) {
        emit logicProblem(tr("%1 changed since; its text was not changed back").arg(moved.join(QStringLiteral(", "))));
    }
    if (!failed.isEmpty()) {
        emit logicProblem(
            tr("%1 could not be written; the logic there was left as it is").arg(failed.join(QStringLiteral(", "))));
    }
}

std::vector<ProfileEditor::LogicEdit> ProfileEditor::changeLogic(const std::vector<CodeRename>& renames,
                                                                 const std::vector<std::string>& gone)
{
    std::vector<LogicEdit> written;
    if (m_logicFolder.isEmpty() || (renames.empty() && gone.empty())) {
        return written;
    }
    const std::filesystem::path folder(m_logicFolder.toStdWString());
    const QStringList unsaved = unsavedLogicFiles();
    const OpenTexts open = openLogicTexts();
    QStringList skipped;
    std::vector<LogicEdit> prepared;
    const QStringList files = logicFilesIn(folder);
    for (const QString& file : files) {
        const bool dirty = listedPath(unsaved, file);
        std::string before;
        const auto opened = std::ranges::find_if(open, [&file](const auto& entry) {
            return QDir::cleanPath(QDir::fromNativeSeparators(entry.first))
                       .compare(QDir::cleanPath(QDir::fromNativeSeparators(file)), Qt::CaseInsensitive) == 0;
        });
        if (opened != open.end()) {
            before.assign(opened->second.constData(), static_cast<std::size_t>(opened->second.size()));
        } else if (dirty || platform::readFile(folder / file.toStdWString(), kMostLogicFileBytes, before,
                                               platform::WhenBusy::GiveUp)) {
            continue;
        }
        std::string after = before;
        int changed = 0;
        for (const CodeRename& rename : renames) {
            ControlRewrite result = renameControlUses(after, rename.from, rename.to);
            changed += result.changed;
            after = std::move(result.text);
        }
        for (const std::string& id : gone) {
            ControlRewrite result = removeControlUses(after, id);
            changed += result.changed;
            after = std::move(result.text);
        }
        if (changed == 0) {
            continue;
        }
        if (dirty) {
            if (!skipped.contains(file)) {
                skipped.append(file);
            }
            continue;
        }
        prepared.push_back({.file = file,
                            .before = QByteArray::fromStdString(before),
                            .after = QByteArray::fromStdString(after)});
    }
    const auto takeFile = [&prepared, &folder](const QString& file) -> std::optional<LogicEdit> {
        if (const auto found = std::ranges::find(prepared, file, &LogicEdit::file); found != prepared.end()) {
            LogicEdit edit = *found;
            prepared.erase(found);
            return edit;
        }
        std::string contents;
        if (platform::readFile(folder / file.toStdWString(), kMostLogicFileBytes, contents,
                               platform::WhenBusy::GiveUp)) {
            return std::nullopt;
        }
        const QByteArray bytes = QByteArray::fromStdString(contents);
        return LogicEdit{.file = file, .before = bytes, .after = bytes};
    };
    const auto componentFiles = [](const std::string& id) {
        const QString code = QStringLiteral("controls/%1.ts").arg(QString::fromStdString(id));
        return std::array<QString, 2>{code, QString::fromStdString(blueprint::layoutFileName(code.toStdString()))};
    };
    for (const CodeRename& rename : renames) {
        const std::array<QString, 2> from = componentFiles(rename.from);
        const std::array<QString, 2> to = componentFiles(rename.to);
        std::error_code error;
        if (std::filesystem::exists(folder / to[0].toStdWString(), error)) {
            continue;
        }
        for (std::size_t part = 0; part < from.size(); ++part) {
            if (listedPath(unsaved, from[part])) {
                continue;
            }
            if (const std::optional<LogicEdit> moving = takeFile(from[part])) {
                prepared.push_back({.file = to[part], .before = {}, .after = moving->after, .existedBefore = false});
                prepared.push_back({.file = from[part], .before = moving->before, .after = {}, .existsAfter = false});
            }
        }
    }
    for (const std::string& id : gone) {
        for (const QString& file : componentFiles(id)) {
            if (const std::optional<LogicEdit> removing = takeFile(file)) {
                prepared.push_back({.file = file, .before = removing->before, .after = {}, .existsAfter = false});
            }
        }
    }
    if (!skipped.isEmpty()) {
        emit logicProblem(
            tr("%1 has unsaved changes; the logic there was left as it is").arg(skipped.join(QStringLiteral(", "))));
    }
    QStringList failed;
    for (const LogicEdit& edit : prepared) {
        if (!edit.existsAfter) {
            continue;
        }
        const std::filesystem::path path = folder / edit.file.toStdWString();
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (platform::writeFileAtomically(path, edit.after.toStdString())) {
            failed.append(edit.file);
            continue;
        }
        written.push_back(edit);
    }
    for (const LogicEdit& edit : prepared) {
        if (edit.existsAfter || !failed.isEmpty()) {
            continue;
        }
        if (platform::removeFile(folder / edit.file.toStdWString())) {
            failed.append(edit.file);
            continue;
        }
        written.push_back(edit);
    }
    if (!failed.isEmpty()) {
        emit logicProblem(
            tr("%1 could not be written; the logic there was left as it is").arg(failed.join(QStringLiteral(", "))));
    }
    return written;
}

QString ProfileEditor::codeNameProblem(const QString& id, const QString& wanted) const
{
    const auto at = indexOf(id);
    if (!at) {
        return tr("That component is no longer there");
    }
    const QString cleaned = wanted.trimmed();
    if (cleaned.isEmpty()) {
        return tr("A code name cannot be empty");
    }
    if (cleaned.size() > static_cast<qsizetype>(input::kNameLength)) {
        return tr("A code name has at most %1 characters").arg(static_cast<int>(input::kNameLength));
    }
    const QChar first = cleaned.front();
    const bool letter = (first >= u'a' && first <= u'z') || (first >= u'A' && first <= u'Z');
    if (!letter && first != u'_') {
        return tr("A code name starts with a letter or _");
    }
    if (!codeNameWord(cleaned.toStdString())) {
        return tr("A code name uses only English letters, digits, _ and -");
    }
    if (codeNameReserved(cleaned.toStdString())) {
        return tr("The logic already uses the word “%1”").arg(cleaned);
    }
    const std::string wantedId = cleaned.toStdString();
    if (std::ranges::any_of(profile().controls, [&](const input::Control& control) {
            return control.id == wantedId && control.id != id.toStdString();
        }) ||
        std::ranges::any_of(profile().vars, [&](const input::Variable& variable) { return variable.id == wantedId; })) {
        return tr("Another component or variable already has the code name “%1”").arg(cleaned);
    }
    return {};
}

bool ProfileEditor::renameCode(const QString& id, const QString& wanted)
{
    const QString cleaned = wanted.trimmed();
    if (cleaned == id || !codeNameProblem(id, cleaned).isEmpty()) {
        return false;
    }
    const auto at = indexOf(id);
    if (!at) {
        return false;
    }
    input::Profile next = profile();
    next.controls[*at].id = cleaned.toStdString();
    next.controls[*at].idFixed = true;
    const std::vector<CodeRename> renames{{id.toStdString(), cleaned.toStdString()}};
    renameReferences(next, renames);
    QStringList selection = m_selection;
    for (QString& chosen : selection) {
        if (chosen == id) {
            chosen = cleaned;
        }
    }
    std::vector<LogicEdit> logic = changeLogic(renames, {});
    commit(std::move(next), selection, std::move(logic), false);
    return true;
}

QStringList ProfileEditor::idsToRemove() const
{
    QStringList removed = m_selection;
    for (const input::Control& control : profile().controls) {
        const QString id = QString::fromStdString(control.id);
        if (!control.parent.empty() && m_selection.contains(QString::fromStdString(control.parent)) &&
            !removed.contains(id)) {
            removed.append(id);
        }
    }
    return removed;
}

int ProfileEditor::logicUses()
{
    const QStringList removed = idsToRemove();
    if (removed.isEmpty() || m_logicFolder.isEmpty()) {
        return 0;
    }
    const std::filesystem::path folder(m_logicFolder.toStdWString());
    const OpenTexts open = openLogicTexts();
    int uses = 0;
    for (const QString& id : removed) {
        uses += static_cast<int>(findControlUses(folder, id.toStdString(), open).found.size());
    }
    return uses;
}

void ProfileEditor::deleteSelection(bool withBlocks)
{
    const QStringList removed = idsToRemove();
    if (removed.isEmpty()) {
        return;
    }
    for (const QString& id : removed) {
        const auto at = indexOf(id);
        if (at) {
            rememberGone(id, displayName(*at));
        }
    }
    std::vector<LogicEdit> logic;
    if (withBlocks) {
        std::vector<std::string> gone;
        for (const QString& id : removed) {
            gone.push_back(id.toStdString());
        }
        logic = changeLogic({}, gone);
    }
    input::Profile next = profile();
    for (const QString& id : removed) {
        const std::string gone = id.toStdString();
        std::erase_if(next.controls, [&](const input::Control& control) { return control.id == gone; });
        forget(next, gone);
    }
    commit(std::move(next), QStringList{}, std::move(logic));
}

QStringList ProfileEditor::duplicateSelection()
{
    if (m_selection.isEmpty()) {
        return {};
    }
    input::Profile next = profile();
    const input::CoordinateMapper map = mapper();
    const auto stepped = [&map](const input::Control& control) {
        const input::Vec2 center = map.position(control.anchor, control.offset);
        return input::Vec2{center.x + kDuplicateStep, center.y + kDuplicateStep};
    };
    QStringList copies;
    for (const QString& id : m_selection) {
        const auto at = indexOf(id);
        if (!at) {
            continue;
        }
        const input::Control& original = profile().controls[*at];
        if (!original.parent.empty() && m_selection.contains(QString::fromStdString(original.parent))) {
            continue;
        }
        input::Control copy = original;
        copy.id = freeId(copy.id, next);
        copy.name = numberedName(stemOf(QString::fromStdString(copy.name)), next, false);
        copy.parent.clear();
        copy.child.clear();
        placeAt(copy, stepped(original));
        const std::string group = copy.id;
        copies.append(QString::fromStdString(group));
        next.controls.push_back(std::move(copy));
        for (const input::Control& member : profile().controls) {
            if (member.parent != original.id) {
                continue;
            }
            input::Control twin = member;
            twin.id = childId(group, QString::fromStdString(member.child), next);
            twin.name = numberedName(stemOf(QString::fromStdString(member.name)), next, false);
            twin.parent = group;
            placeAt(twin, stepped(member));
            next.controls.push_back(std::move(twin));
        }
    }
    commit(std::move(next), copies);
    return copies;
}

bool ProfileEditor::copySelection()
{
    if (m_selection.isEmpty()) {
        return false;
    }
    const input::CoordinateMapper map = mapper();
    std::vector<CopiedGroup> taken;
    for (const QString& id : m_selection) {
        const auto at = indexOf(id);
        if (!at) {
            continue;
        }
        const input::Control& original = profile().controls[*at];
        if (!original.parent.empty() && m_selection.contains(QString::fromStdString(original.parent))) {
            continue;
        }
        CopiedGroup group{.head = {.control = original, .center = map.position(original.anchor, original.offset)},
                          .members = {}};
        group.head.control.parent.clear();
        group.head.control.child.clear();
        for (const input::Control& member : profile().controls) {
            if (member.parent == original.id) {
                group.members.push_back({.control = member, .center = map.position(member.anchor, member.offset)});
            }
        }
        taken.push_back(std::move(group));
    }
    if (taken.empty()) {
        return false;
    }
    copiedKeys() = std::move(taken);
    return true;
}

bool ProfileEditor::canPaste() const
{
    return !copiedKeys().empty();
}

QStringList ProfileEditor::pasteAt(double x, double y)
{
    const std::vector<CopiedGroup>& keys = copiedKeys();
    if (keys.empty()) {
        return {};
    }
    double sumX = 0.0;
    double sumY = 0.0;
    for (const CopiedGroup& group : keys) {
        sumX += group.head.center.x;
        sumY += group.head.center.y;
    }
    const double count = static_cast<double>(keys.size());
    const input::Vec2 shift{std::clamp(x, 0.0, 1.0) - sumX / count, std::clamp(y, 0.0, 1.0) - sumY / count};
    const auto moved = [&shift](input::Vec2 center) {
        return input::Vec2{std::clamp(center.x + shift.x, 0.0, 1.0), std::clamp(center.y + shift.y, 0.0, 1.0)};
    };
    input::Profile next = profile();
    QStringList pasted;
    for (const CopiedGroup& group : keys) {
        input::Control copy = group.head.control;
        copy.id = freeId(copy.id, next);
        copy.name = numberedName(stemOf(QString::fromStdString(copy.name)), next, false);
        placeAt(copy, moved(group.head.center));
        const std::string head = copy.id;
        pasted.append(QString::fromStdString(head));
        next.controls.push_back(std::move(copy));
        for (const CopiedKey& member : group.members) {
            input::Control twin = member.control;
            twin.id = childId(head, QString::fromStdString(member.control.child), next);
            twin.name = numberedName(stemOf(QString::fromStdString(member.control.name)), next, false);
            twin.parent = head;
            placeAt(twin, moved(member.center));
            next.controls.push_back(std::move(twin));
        }
    }
    commit(std::move(next), pasted);
    return pasted;
}

bool ProfileEditor::setValue(const QString& id, const QString& path, const QVariant& value)
{
    const auto at = indexOf(id);
    if (!at) {
        return false;
    }
    std::optional<input::Profile> next = withValue(profile(), *at, path, value);
    if (!next) {
        return false;
    }
    commit(std::move(*next));
    return true;
}

QVariant ProfileEditor::value(const QString& id, const QString& path) const
{
    const auto at = indexOf(id);
    if (!at) {
        return {};
    }
    const Json root = Json::parse(input::saveProfile(profile()), nullptr, false);
    if (root.is_discarded() || !root.contains("controls") || *at >= root["controls"].size()) {
        return {};
    }
    const Json* found = find(root["controls"][*at], segmentsOf(path));
    return found != nullptr ? toVariant(*found) : QVariant();
}

QVariantList ProfileEditor::behavior(const QString& id) const
{
    const auto at = indexOf(id);
    if (!at) {
        return {};
    }
    return form::behavior({.profile = profile(), .index = *at, .mapper = mapper(), .width = m_screen.width});
}

bool ProfileEditor::setField(const QString& id, const QString& key, const QVariant& value)
{
    if (key.startsWith(u"prop:")) {
        return setPlacedProp(id, key.mid(5), value);
    }
    if (key.startsWith(u"slot:")) {
        return setBehaviorInput(id, key.mid(5), value.toString());
    }
    const auto at = indexOf(id);
    if (!at) {
        return false;
    }
    const form::Context context{.profile = profile(), .index = *at, .mapper = mapper(), .width = m_screen.width};
    if (key == u"when") {
        return setWhen(id, value.toString());
    }
    const form::Field* field = form::find(context, key.toStdString());
    if (field != nullptr && field->shown == form::Shown::MouseBound) {
        const QString claim = value.toString();
        if (std::ranges::find(input::kClaimNames, claim.toStdString()) == input::kClaimNames.end()) {
            return false;
        }
        std::optional<input::Profile> next = withValue(profile(), *at, nameOf(field->path), claim);
        if (!next) {
            return false;
        }
        commit(std::move(*next));
        return true;
    }
    if (field == nullptr || m_screen.width <= 0.0) {
        return false;
    }
    const input::Control& control = profile().controls[*at];
    const std::vector<form::Declared> values = form::declaredOf(control);
    const form::Declared* stored = form::find(values, field->path);
    const QString path = nameOf(field->path);
    const double number = value.toDouble();
    const auto pairAt = [&] {
        const auto* pair = stored != nullptr ? std::get_if<input::Vec2>(&stored->value) : nullptr;
        return pair != nullptr ? *pair : input::Vec2{};
    };
    const auto withPart = [&](input::Vec2 point, double part) {
        (field->component == 1 ? point.y : point.x) = part;
        return point;
    };
    std::optional<input::Profile> next;
    switch (field->measure) {
    case form::Measure::PositionX:
    case form::Measure::PositionY:
        next = profile();
        placeAt(next->controls[*at], withPart(mapper().position(control.anchor, control.offset), number / 100.0));
        carryChildren(*next);
        break;
    case form::Measure::ExtentX:
    case form::Measure::ExtentY: {
        const input::Vec2 extent = withPart(mapper().extent(pairAt()), number / 100.0);
        next = withValue(profile(), *at, path, areaValue(extent.x, extent.y));
        break;
    }
    case form::Measure::Length:
        next = withValue(profile(), *at, path, lengthValue(number / m_screen.width));
        break;
    case form::Measure::HeroX:
    case form::Measure::HeroY: {
        const input::Vec2 point = withPart(mapper().position(input::Anchor::Center, pairAt()), number / 100.0);
        next = withValue(profile(), *at, path, pointValue(point.x, point.y));
        break;
    }
    case form::Measure::Anchor: {
        const auto found = std::ranges::find(input::kAnchorNames, value.toString().toStdString());
        if (found == input::kAnchorNames.end()) {
            return false;
        }
        next = profile();
        input::Control& placed = next->controls[*at];
        const input::Vec2 position = mapper().position(placed.anchor, placed.offset);
        placed.anchor = static_cast<input::Anchor>(std::distance(input::kAnchorNames.begin(), found));
        const input::Vec2 offset = mapper().offsetOf(placed.anchor, position);
        placed.offset = {fine(offset.x), fine(offset.y)};
        break;
    }
    case form::Measure::Plain:
        if (field->component >= 0) {
            next = withValue(profile(), *at, path, pairOf(withPart(pairAt(), number)));
        } else if (stored != nullptr && stored->whole) {
            next = withValue(profile(), *at, path, QVariant::fromValue<qint64>(std::llround(number)));
        } else {
            next = withValue(profile(), *at, path, number);
        }
        break;
    case form::Measure::Choice:
        next = withValue(profile(), *at, path, value.toString());
        break;
    case form::Measure::Flag:
        next = withValue(profile(), *at, path, value.toBool());
        break;
    case form::Measure::Pauses: {
        const std::optional<std::string> walker = value.toBool() ? walkerOf(profile()) : std::nullopt;
        if (value.toBool() && !walker) {
            return false;
        }
        next = withValue(profile(), *at, path,
                         walker ? QVariant(QString::fromStdString(*walker)) : QVariant());
        break;
    }
    case form::Measure::Places: {
        if (!std::holds_alternative<input::MultiPressControl>(control.kind)) {
            return false;
        }
        const auto wanted = static_cast<std::size_t>(
            std::clamp<long long>(std::llround(number), 2, static_cast<long long>(input::kMostMultiPressPoints) + 1) - 1);
        next = profile();
        std::vector<input::Vec2>& points = std::get<input::MultiPressControl>(next->controls[*at].kind).points;
        while (points.size() > wanted) {
            points.pop_back();
        }
        while (points.size() < wanted) {
            const input::Vec2 last = points.empty() ? input::Vec2{} : points.back();
            points.push_back({std::clamp(fine(last.x + 0.08), -2.0, 2.0), last.y});
        }
        break;
    }
    case form::Measure::Input:
        return false;
    }
    if (!next) {
        return false;
    }
    commitEdit(std::move(*next), id, key, form::numeric(*field));
    return true;
}

bool ProfileEditor::setKey(const QString& id, const QString& binding, const QString& input)
{
    const auto at = indexOf(id);
    if (!at || input.isEmpty()) {
        return false;
    }
    if (!binding.startsWith(QStringLiteral("inputs."))) {
        return setValue(id, binding, input);
    }
    const QString prefix = binding + u'.';
    std::optional<input::Profile> next = profile();
    const std::optional<Json> entry = entryOf(profile(), *at);
    if (!input::claimable(input::parseChord(input.toStdString())) && entry &&
        find(*entry, segmentsOf(prefix + QStringLiteral("claim"))) != nullptr) {
        next = withValue(*next, *at, prefix + QStringLiteral("claim"), QVariant());
    }
    if (next) {
        next = withValue(*next, *at, prefix + QStringLiteral("input"), input);
    }
    if (!next) {
        return false;
    }
    commit(std::move(*next));
    return true;
}

bool ProfileEditor::clearKey(const QString& id, const QString& binding)
{
    const auto at = indexOf(id);
    if (!at) {
        return false;
    }
    if (!binding.startsWith(QStringLiteral("inputs."))) {
        return setValue(id, binding, QVariant());
    }
    std::optional<input::Profile> next = withValue(profile(), *at, binding, QVariant());
    if (!next) {
        return false;
    }
    commit(std::move(*next));
    return true;
}

bool ProfileEditor::setGamepadPassthrough(bool on)
{
    input::Profile next = profile();
    next.gamepad = on ? input::GamepadMode::Passthrough : input::GamepadMode::Map;
    if (next == profile()) {
        return false;
    }
    commit(std::move(next));
    return true;
}

bool ProfileEditor::setWhen(const QString& id, const QString& when)
{
    const auto at = indexOf(id);
    if (!at) {
        return false;
    }
    std::optional<input::Profile> next =
        withValue(profile(), *at, QStringLiteral("when"), when.isEmpty() ? QVariant() : QVariant(when));
    if (!next) {
        return false;
    }
    commit(std::move(*next));
    return true;
}

QString ProfileEditor::placedRef(const QString& id) const
{
    const auto at = indexOf(id);
    return at ? QString::fromStdString(input::componentRef(profile().controls[*at])) : QString();
}

QVariantMap ProfileEditor::addPlaced(const QString& ref, double x, double y)
{
    const ComponentLibrary* library = ComponentLibrary::instance();
    const ComponentEntry* entry = library != nullptr ? library->find(ref) : nullptr;
    if (entry == nullptr) {
        return {};
    }
    if (!entry->extensionId.isEmpty()) {
        emit logicProblem(tr("%1 comes with an extension: copy it to your components to use it").arg(entry->name));
        return {};
    }
    QString problem;
    if (logicFolder(problem).isEmpty()) {
        emit logicProblem(problem);
        return {};
    }
    input::Profile next = profile();
    input::Control control;
    std::string base;
    for (const QChar c : ref.section(u'/', 1)) {
        if (c.isLetterOrNumber() && c.unicode() < 0x80) {
            base += static_cast<char>(c.unicode());
        } else if (c == u'-' || c == u'_') {
            base += static_cast<char>(c.unicode());
        }
    }
    control.id = freeId(base.empty() ? std::string_view("component") : std::string_view(base), next);
    control.name = numberedName(entry->name, next, true);
    std::vector<input::NamedBinding> keys;
    std::vector<input::NamedValue> props;
    QString first;
    int points = 0;
    for (const DeclaredSpec& spec : entry->props) {
        const std::string name = spec.name.toStdString();
        if (spec.kind == u"key") {
            std::optional<input::Binding> binding;
            if (const std::optional<input::Chord> chord = input::parseChord(spec.value.toString().toStdString())) {
                binding = input::Binding{.input = *chord};
            }
            keys.push_back({name, binding});
            if (first.isEmpty()) {
                first = QStringLiteral("inputs.") + spec.name;
            }
        } else if (spec.kind == u"number") {
            props.push_back({name, spec.value.toDouble()});
        } else if (spec.kind == u"text") {
            props.push_back({name, spec.value.toString().toStdString()});
        } else if (spec.kind == u"toggle") {
            props.push_back({name, spec.value.toBool()});
        } else if (spec.kind == u"position") {
            ++points;
            props.push_back({name, input::Vec2{0.12 * points, 0.0}});
        }
    }
    const std::string type = entry->type.toStdString();
    const auto recipe = std::ranges::find(recipes(), std::string_view(type), &Recipe::kind);
    const bool extended = !type.empty() && recipe != recipes().end();
    QString slot = first;
    if (extended) {
        control.kind = *kindFor(recipe->kind);
        if (auto* camera = std::get_if<input::CameraControl>(&control.kind)) {
            camera->lockKey = m_defaultLockKey;
        }
    } else {
        control.kind = input::ComponentControl{.component = ref.toStdString(), .keys = keys, .props = props};
    }
    placeAt(control, {x, y});
    QString id = QString::fromStdString(control.id);
    next.controls.push_back(std::move(control));
    const std::size_t index = next.controls.size() - 1;
    if (extended) {
        giveDefaults(next, index, recipe->defaults);
        slot = QString::fromLatin1(recipe->slot);
        adoptSaved(next, index, entry->type, slot);
        input::Control& made = next.controls[index];
        made.component = ref.toStdString();
        made.keys = std::move(keys);
        made.props = std::move(props);
    }
    for (const DeclaredSpec& spec : entry->props) {
        const QString path = QStringLiteral("inputs.") + spec.name;
        if (spec.kind == u"key" && clashes(next, index, path)) {
            next = withoutInput(next, index, path);
        }
    }
    if (!slot.isEmpty()) {
        const std::optional<Json> now = entryOf(next, index);
        if (now && bound(find(*now, segmentsOf(slot)))) {
            slot.clear();
        }
    }
    {
        input::Control& made = next.controls[index];
        const std::string part = keyPartOf(made);
        if (!part.empty() && made.component.empty() && !std::holds_alternative<input::ComponentControl>(made.kind) &&
            !std::holds_alternative<input::BehaviorControl>(made.kind)) {
            made.id = uniqueId(codeBaseOf(made) + part, next, made.id);
            id = QString::fromStdString(made.id);
        }
    }
    const input::Control& group = next.controls[index];    const input::Vec2 center = mapper().position(group.anchor, group.offset);
    for (std::size_t k = 0; k < entry->children.size(); ++k) {
        addChild(next, id.toStdString(), center, entry->children[k], k, entry->children.size());
    }
    commit(std::move(next), QStringList{id});
    return {{QStringLiteral("id"), id}, {QStringLiteral("slot"), slot}};
}

QVariantList ProfileEditor::placedFields(std::size_t index) const
{
    const input::Control& control = profile().controls[index];
    const ComponentEntry* entry = classOf(control);
    if (entry == nullptr) {
        return {};
    }
    const std::vector<input::NamedBinding>& keys = input::componentKeys(control);
    const std::vector<input::NamedValue>& props = input::componentProps(control);
    const std::string section = "$.controls[" + std::to_string(index) + "].";
    QVariantList fields;
    for (const DeclaredSpec& spec : entry->props) {
        const std::string name = spec.name.toStdString();
        const QString label = spec.label.isEmpty() ? spec.name : spec.label;
        QVariantMap field{{QStringLiteral("key"), QStringLiteral("prop:") + spec.name},
                          {QStringLiteral("title"), label},
                          {QStringLiteral("hint"), QString()},
                          {QStringLiteral("half"), false},
                          {QStringLiteral("enabled"), true}};
        const auto stored = std::ranges::find(props, name, &input::NamedValue::name);
        const input::PropValue* value = stored != props.end() ? &stored->value : nullptr;
        if (spec.kind == u"key") {
            const auto key = std::ranges::find(keys, name, &input::NamedBinding::name);
            const std::optional<input::Chord> chord = key != keys.end() && key->binding ? key->binding->input : std::nullopt;
            const QString path = QStringLiteral("inputs.") + spec.name;
            field.insert(QStringLiteral("widget"), QStringLiteral("key"));
            field.insert(QStringLiteral("slot"), path);
            field.insert(QStringLiteral("keys"), capsOf(chord));
            field.insert(QStringLiteral("bindPath"), path);
            field.insert(QStringLiteral("clearPath"), path);
            field.insert(QStringLiteral("conflict"), chord ? conflictText(section + path.toStdString()) : QString());
        } else if (spec.kind == u"position") {
            field.insert(QStringLiteral("widget"), QStringLiteral("note"));
            field.insert(QStringLiteral("value"), tr("Drag the dot for “%1” on the picture").arg(label));
        } else if (spec.kind == u"number") {
            const double* number = value != nullptr ? std::get_if<double>(value) : nullptr;
            const double shown = number != nullptr ? *number : spec.value.toDouble();
            const bool whole = std::floor(spec.value.toDouble()) == spec.value.toDouble();
            const double low = spec.min.value_or(-1000000.0);
            const double high = spec.max.value_or(1000000.0);
            field.insert(QStringLiteral("widget"), QStringLiteral("number"));
            field.insert(QStringLiteral("value"), shown);
            field.insert(QStringLiteral("minimum"), low);
            field.insert(QStringLiteral("maximum"), high);
            field.insert(QStringLiteral("from"), low);
            field.insert(QStringLiteral("to"), high);
            field.insert(QStringLiteral("logarithmic"), false);
            field.insert(QStringLiteral("step"), whole ? 1.0 : 0.1);
            field.insert(QStringLiteral("decimals"), whole ? 0 : 2);
            field.insert(QStringLiteral("unit"), QString());
            field.insert(QStringLiteral("word"), QString());
        } else if (spec.kind == u"text") {
            const std::string* text = value != nullptr ? std::get_if<std::string>(value) : nullptr;
            field.insert(QStringLiteral("widget"), QStringLiteral("text"));
            field.insert(QStringLiteral("value"), text != nullptr ? QString::fromStdString(*text) : spec.value.toString());
        } else if (spec.kind == u"toggle") {
            const bool* on = value != nullptr ? std::get_if<bool>(value) : nullptr;
            field.insert(QStringLiteral("widget"), QStringLiteral("switch"));
            field.insert(QStringLiteral("value"), on != nullptr ? *on : spec.value.toBool());
        } else if (spec.kind == u"component") {
            const std::string* target = value != nullptr ? std::get_if<std::string>(value) : nullptr;
            QVariantList options{QVariantMap{{QStringLiteral("value"), QString()}, {QStringLiteral("text"), tr("None")}}};
            for (std::size_t other = 0; other < profile().controls.size(); ++other) {
                const input::Control& candidate = profile().controls[other];
                if (other != index && (spec.of.isEmpty() || nameOf(input::kControlTypeNames[candidate.kind.index()]) == spec.of)) {
                    options.append(QVariantMap{{QStringLiteral("value"), QString::fromStdString(candidate.id)},
                                               {QStringLiteral("text"), displayName(other)}});
                }
            }
            field.insert(QStringLiteral("widget"), QStringLiteral("select"));
            field.insert(QStringLiteral("options"), options);
            field.insert(QStringLiteral("value"), target != nullptr ? QString::fromStdString(*target) : QString());
        } else {
            continue;
        }
        fields.append(field);
    }
    return fields;
}

bool ProfileEditor::setPlacedProp(const QString& id, const QString& name, const QVariant& value)
{
    const auto at = indexOf(id);
    const ComponentEntry* entry = at ? classOf(profile().controls[*at]) : nullptr;
    if (entry == nullptr) {
        return false;
    }
    const auto spec = std::ranges::find(entry->props, name, &DeclaredSpec::name);
    if (spec == entry->props.end()) {
        return false;
    }
    QVariant stored;
    if (spec->kind == u"number") {
        bool ok = false;
        double number = value.toDouble(&ok);
        if (!ok || !std::isfinite(number)) {
            return false;
        }
        number = std::clamp(number, spec->min.value_or(-1000000.0), spec->max.value_or(1000000.0));
        stored = number;
    } else if (spec->kind == u"toggle") {
        stored = value.toBool();
    } else if (spec->kind == u"text") {
        stored = value.toString().left(static_cast<qsizetype>(input::kPropTextLength));
    } else if (spec->kind == u"component") {
        const QString target = value.toString();
        const auto found = target.isEmpty() ? std::optional<std::size_t>() : indexOf(target);
        if (!target.isEmpty() &&
            (!found || target == id ||
             (!spec->of.isEmpty() && nameOf(input::kControlTypeNames[profile().controls[*found].kind.index()]) != spec->of))) {
            return false;
        }
        stored = target.isEmpty() ? QVariant() : QVariant(target);
    } else {
        return false;
    }
    std::optional<input::Profile> next = withValue(profile(), *at, QStringLiteral("props.") + name, stored);
    if (!next) {
        return false;
    }
    commit(std::move(*next));
    return true;
}

bool ProfileEditor::setPlacedPoint(const QString& id, const QString& name, double x, double y)
{
    if (name.startsWith(u"module:")) {
        return setModulePoint(id, name, x, y);
    }
    const auto at = indexOf(id);
    if (!at) {
        return false;
    }
    const input::Control& control = profile().controls[*at];
    const std::string wanted = name.toStdString();
    if (const auto* wheel = std::get_if<input::ActionWheelControl>(&control.kind)) {
        const QString number = name.startsWith(u"cells.") ? name.mid(6) : QString();
        bool read = false;
        const int index = number.toInt(&read);
        if (!read || index < 0 || static_cast<std::size_t>(index) >= wheel->cells.size()) {
            return false;
        }
        const input::CoordinateMapper geometry = mapper();
        const input::Vec2 origin = geometry.position(control.anchor, control.offset);
        const input::Vec2 point = clamped({x, y});
        const input::Vec2 step = geometry.sizeOf({point.x - origin.x, point.y - origin.y});
        input::Profile next = profile();
        std::get<input::ActionWheelControl>(next.controls[*at].kind).cells[static_cast<std::size_t>(index)].at = {
            std::clamp(fine(step.x), -2.0, 2.0), std::clamp(fine(step.y), -2.0, 2.0)};
        commit(std::move(next));
        return true;
    }
    const auto* multi = std::get_if<input::MultiPressControl>(&control.kind);
    if (multi != nullptr) {
        const QString number = name.startsWith(u"points.") ? name.mid(7) : QString();
        bool read = false;
        const int index = number.toInt(&read);
        if (!read || index < 0 || static_cast<std::size_t>(index) >= multi->points.size()) {
            return false;
        }
    } else if (input::componentRef(control).empty() ||
               std::ranges::none_of(input::componentProps(control), [&](const input::NamedValue& entry) {
                   return entry.name == wanted && std::holds_alternative<input::Vec2>(entry.value);
               })) {
        return false;
    }
    const input::CoordinateMapper geometry = mapper();
    const input::Vec2 origin = geometry.position(control.anchor, control.offset);
    const input::Vec2 point = clamped({x, y});
    return setValue(id, multi != nullptr ? name : QStringLiteral("props.") + name,
                    pairOf(geometry.sizeOf({point.x - origin.x, point.y - origin.y})));
}

QVariantList ProfileEditor::wheelCells(const QString& id) const
{
    const auto at = indexOf(id);
    if (!at) {
        return {};
    }
    const auto* wheel = std::get_if<input::ActionWheelControl>(&profile().controls[*at].kind);
    if (wheel == nullptr) {
        return {};
    }
    QVariantList cells;
    for (const input::WheelCell& cell : wheel->cells) {
        const std::string_view action = input::kWheelActionNames[static_cast<std::size_t>(cell.action)];
        cells.append(QVariantMap{{QStringLiteral("name"), QString::fromStdString(cell.name)},
                                 {QStringLiteral("action"),
                                  QString::fromUtf8(action.data(), static_cast<qsizetype>(action.size()))},
                                 {QStringLiteral("control"), QString::fromStdString(cell.control)}});
    }
    return cells;
}

QVariantList ProfileEditor::wheelTargets(const QString& id) const
{
    QVariantList targets;
    for (const input::Control& control : profile().controls) {
        const bool fits = std::holds_alternative<input::TapControl>(control.kind) ||
                          std::holds_alternative<input::HoldControl>(control.kind) ||
                          std::holds_alternative<input::LongPressControl>(control.kind) ||
                          std::holds_alternative<input::TurboControl>(control.kind) ||
                          std::holds_alternative<input::SwipeControl>(control.kind) ||
                          std::holds_alternative<input::MultiPressControl>(control.kind) ||
                          std::holds_alternative<input::ComponentControl>(control.kind);
        const QString controlId = QString::fromStdString(control.id);
        if (!fits || controlId == id) {
            continue;
        }
        targets.append(QVariantMap{{QStringLiteral("id"), controlId},
                                   {QStringLiteral("name"),
                                    QString::fromStdString(control.name.empty() ? control.id : control.name)}});
    }
    return targets;
}

bool ProfileEditor::setWheelCell(const QString& id, int cell, const QString& key, const QVariant& value)
{
    const auto at = indexOf(id);
    if (!at) {
        return false;
    }
    input::Profile next = profile();
    auto* wheel = std::get_if<input::ActionWheelControl>(&next.controls[*at].kind);
    if (wheel == nullptr || cell < 0 || static_cast<std::size_t>(cell) >= wheel->cells.size()) {
        return false;
    }
    input::WheelCell& target = wheel->cells[static_cast<std::size_t>(cell)];
    if (key == u"name") {
        target.name = value.toString().trimmed().left(static_cast<qsizetype>(input::kWheelCellNameChars)).toStdString();
    } else if (key == u"action") {
        const std::string wanted = value.toString().toStdString();
        const auto found = std::ranges::find(input::kWheelActionNames, wanted);
        if (found == input::kWheelActionNames.end()) {
            return false;
        }
        target.action = static_cast<input::WheelAction>(std::distance(input::kWheelActionNames.begin(), found));
    } else if (key == u"control") {
        const std::string wanted = value.toString().toStdString();
        if (!wanted.empty() && std::ranges::none_of(next.controls, [&](const input::Control& other) {
                return other.id == wanted;
            })) {
            return false;
        }
        target.control = wanted;
    } else {
        return false;
    }
    if (next == profile()) {
        return true;
    }
    commit(std::move(next));
    return true;
}

bool ProfileEditor::setWheelCellCount(const QString& id, int count)
{
    const auto at = indexOf(id);
    if (!at) {
        return false;
    }
    input::Profile next = profile();
    auto* wheel = std::get_if<input::ActionWheelControl>(&next.controls[*at].kind);
    if (wheel == nullptr) {
        return false;
    }
    const auto wanted = std::clamp<std::size_t>(static_cast<std::size_t>(std::max(count, 0)), input::kFewestWheelCells,
                                                input::kMostWheelCells);
    if (wheel->cells.size() == wanted) {
        return true;
    }
    wheel->cells.resize(wanted);
    commit(std::move(next));
    return true;
}

void ProfileEditor::addChild(input::Profile& next, const std::string& group, input::Vec2 center,
                             const ComponentChild& child, std::size_t index, std::size_t count) const
{
    const std::string type = child.type.toStdString();
    const auto recipe = std::ranges::find(recipes(), std::string_view(type), &Recipe::kind);
    if (recipe == recipes().end()) {
        return;
    }
    const auto owner = std::ranges::find(next.controls, group, &input::Control::id);
    const QString title = owner == next.controls.end() || owner->name.empty() ? QString::fromStdString(group)
                                                                              : QString::fromStdString(owner->name);
    input::Control control;
    control.id = childId(group, child.name, next);
    control.name = numberedName(title + QStringLiteral(" · ") + child.name, next, true);
    control.kind = *kindFor(recipe->kind);
    if (auto* camera = std::get_if<input::CameraControl>(&control.kind)) {
        camera->lockKey = m_defaultLockKey;
    }
    control.parent = group;
    control.child = child.name.toStdString();
    placeAt(control, aroundGroup(mapper(), center, index, count));
    next.controls.push_back(std::move(control));
    giveDefaults(next, next.controls.size() - 1, recipe->defaults);
}

bool ProfileEditor::syncChildren(input::Profile& next) const
{
    bool changed = false;
    for (input::Control& control : next.controls) {
        if (control.parent.empty()) {
            continue;
        }
        const auto group = std::ranges::find(next.controls, control.parent, &input::Control::id);
        const ComponentEntry* entry = group != next.controls.end() ? classOf(*group) : nullptr;
        if (group != next.controls.end() && entry == nullptr) {
            continue;
        }
        const bool declared = entry != nullptr && std::ranges::any_of(entry->children, [&](const ComponentChild& child) {
            return child.name.toStdString() == control.child &&
                   child.type.toStdString() == input::kControlTypeNames[control.kind.index()];
        });
        if (!declared) {
            control.parent.clear();
            control.child.clear();
            changed = true;
        }
    }
    const std::size_t count = next.controls.size();
    for (std::size_t i = 0; i < count; ++i) {
        const ComponentEntry* entry = classOf(next.controls[i]);
        if (entry == nullptr) {
            continue;
        }
        const std::string group = next.controls[i].id;
        const input::Vec2 center = mapper().position(next.controls[i].anchor, next.controls[i].offset);
        for (std::size_t k = 0; k < entry->children.size(); ++k) {
            const std::string name = entry->children[k].name.toStdString();
            const bool present = std::ranges::any_of(next.controls, [&](const input::Control& other) {
                return other.parent == group && other.child == name;
            });
            if (!present && !entry->children[k].type.isEmpty()) {
                addChild(next, group, center, entry->children[k], k, entry->children.size());
                changed = true;
            }
        }
    }
    return changed;
}

void ProfileEditor::carryChildren(input::Profile& next) const
{
    const input::CoordinateMapper map = mapper();
    const std::vector<input::Control>& before = profile().controls;
    const auto old = [&before](const std::string& id) { return std::ranges::find(before, id, &input::Control::id); };
    for (const input::Control& group : next.controls) {
        const auto was = old(group.id);
        if (was == before.end() || samePlace(*was, group)) {
            continue;
        }
        const input::Vec2 from = map.position(was->anchor, was->offset);
        const input::Vec2 to = map.position(group.anchor, group.offset);
        for (input::Control& child : next.controls) {
            const auto stayed = old(child.id);
            if (&child == &group || child.parent != group.id || stayed == before.end() || !samePlace(*stayed, child)) {
                continue;
            }
            const input::Vec2 at = map.position(child.anchor, child.offset);
            placeAt(child, {at.x + to.x - from.x, at.y + to.y - from.y});
        }
    }
}

QString ProfileEditor::componentOf(const QString& id) const
{
    const auto at = indexOf(id);
    if (!at || !input::componentRef(profile().controls[*at]).empty()) {
        return {};
    }
    return nameOf(input::kControlTypeNames[profile().controls[*at].kind.index()]);
}

bool ProfileEditor::saveAsDefault(const QString& id)
{
    if (m_preview) {
        return false;
    }
    ComponentDefaults* defaults = ComponentDefaults::instance();
    const QString component = componentOf(id);
    const auto at = indexOf(id);
    if (defaults == nullptr || component.isEmpty() || !at) {
        return false;
    }
    std::optional<Json> entry = entryOf(profile(), *at);
    if (!entry) {
        return false;
    }
    for (const char* key : {"id", "name", "anchor", "offset", "enabled", "when"}) {
        entry->erase(key);
    }
    const bool stored = defaults->store(component.toStdString(), *entry);
    if (stored && defaults->lastSaveBusy()) {
        emit defaultsBusy();
    }
    return stored;
}

bool ProfileEditor::hasSavedDefault(const QString& id) const
{
    const ComponentDefaults* defaults = ComponentDefaults::instance();
    const QString component = componentOf(id);
    return defaults != nullptr && !component.isEmpty() && defaults->contains(component.toStdString());
}

bool ProfileEditor::restoreFactoryDefault(const QString& id)
{
    ComponentDefaults* defaults = ComponentDefaults::instance();
    const QString component = componentOf(id);
    if (defaults == nullptr || component.isEmpty() || !defaults->forget(component.toStdString())) {
        return false;
    }
    if (defaults->lastSaveBusy()) {
        emit defaultsBusy();
    }
    return true;
}

void ProfileEditor::undo()
{
    if (m_preview || !canUndo()) {
        return;
    }
    m_lastEdit.reset();
    endGesture();
    const QStringList before = m_selection;
    restoreLogic(m_history[m_at].logic, false);
    m_selection = m_history[m_at].selectionBefore;
    --m_at;
    refresh(before);
}

void ProfileEditor::redo()
{
    if (m_preview || !canRedo()) {
        return;
    }
    m_lastEdit.reset();
    endGesture();
    const QStringList before = m_selection;
    ++m_at;
    restoreLogic(m_history[m_at].logic, true);
    m_selection = m_history[m_at].selectionAfter;
    refresh(before);
}

void ProfileEditor::setLogic(QString folder, QString blocked, LogicFolder ensure)
{
    m_logicFolder = std::move(folder);
    m_logicBlocked = std::move(blocked);
    m_ensureLogic = std::move(ensure);
    emit logicChanged();
}

void ProfileEditor::attachLogic(QObject* logic)
{
    if (logic == m_logic) {
        return;
    }
    if (logic != nullptr) {
        logic->setParent(this);
    }
    m_logic = logic;
    if (m_logic == nullptr) {
        m_logicShown = false;
    }
    emit logicShownChanged();
}

void ProfileEditor::setHandedOver(const QStringList& controls)
{
    if (controls == m_handedOver) {
        return;
    }
    m_handedOver = controls;
    emit handedOverChanged();
}

void ProfileEditor::setPreview(Preview plan)
{
    m_preview = std::move(plan);
    m_previewMarks = draftControlChanges(m_preview->live, profile());
    ++m_revision;
    emit previewChanged();
    emit changed();
}

QString ProfileEditor::previewClient() const
{
    return m_preview ? m_preview->client : QString();
}

QString ProfileEditor::previewTarget() const
{
    return m_preview ? m_preview->target : QString();
}

QString ProfileEditor::liveTextOf(const QString& path) const
{
    if (!m_preview) {
        return {};
    }
    return readPreviewText(m_preview->liveFolder, path).value_or(QString());
}

QVariantList ProfileEditor::codeDiff(const QString& path) const
{
    if (!m_preview) {
        return {};
    }
    const std::optional<QString> after = readPreviewText(m_preview->draftFolder, path);
    if (!after) {
        return {};
    }
    const std::optional<QString> before = m_preview->liveFolder.isEmpty() ? std::nullopt
                                                                         : readPreviewText(m_preview->liveFolder, path);
    return draftDiffList(draftTextDiff(before, after));
}

bool ProfileEditor::showLogic(bool on)
{
    if (!on) {
        if (m_logicShown) {
            m_logicShown = false;
            emit logicShownChanged();
        }
        return true;
    }
    if (m_logic == nullptr) {
        QString problem;
        const QString folder = logicFolder(problem);
        if (folder.isEmpty()) {
            emit logicProblem(problem);
            return false;
        }
        emit logicWanted(folder);
        if (m_logic == nullptr) {
            return false;
        }
    }
    if (m_trying) {
        m_trying = false;
        emit tryingChanged();
    }
    if (!m_logicShown) {
        m_logicShown = true;
        emit logicShownChanged();
    }
    return true;
}

QString ProfileEditor::logicFolder(QString& problem)
{
    if (!m_logicBlocked.isEmpty()) {
        problem = m_logicBlocked;
        return {};
    }
    if (m_ensureLogic) {
        const QString folder = m_ensureLogic(problem);
        if (!folder.isEmpty()) {
            m_logicFolder = folder;
        }
        return folder;
    }
    if (m_logicFolder.isEmpty()) {
        problem = tr("The logic of this profile cannot be opened");
    }
    return m_logicFolder;
}

std::optional<std::size_t> ProfileEditor::indexOf(const QString& id) const
{
    const std::string wanted = id.toStdString();
    const auto& controls = profile().controls;
    const auto found = std::ranges::find(controls, wanted, &input::Control::id);
    if (found == controls.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(controls.begin(), found));
}

QString ProfileEditor::nameFor(const QString& kind)
{
    if (kind == u"tap") {
        return tr("Tap");
    }
    if (kind == u"hold") {
        return tr("Hold");
    }
    if (kind == u"longPress") {
        return tr("Long press");
    }
    if (kind == u"turbo") {
        return tr("Turbo");
    }
    if (kind == u"swipe") {
        return tr("Swipe");
    }
    if (kind == u"pinch") {
        return tr("Pinch");
    }
    if (kind == u"wasd") {
        return tr("WASD stick");
    }
    if (kind == u"rightWalk") {
        return tr("Right-click walk");
    }
    if (kind == u"padStick") {
        return tr("Gamepad stick");
    }
    if (kind == u"camera") {
        return tr("View");
    }
    if (kind == u"padCamera") {
        return tr("Gamepad view");
    }
    if (kind == u"freeLook") {
        return tr("Free look");
    }
    if (kind == u"skill") {
        return tr("Skill");
    }
    if (kind == u"dial") {
        return tr("Direction wheel");
    }
    if (kind == u"multiPress") {
        return tr("Multi-press");
    }
    if (kind == u"tempCursor") {
        return tr("Temporary cursor");
    }
    if (kind == u"actionWheel") {
        return tr("Action wheel");
    }
    return {};
}

QString ProfileEditor::displayName(std::size_t index) const
{
    if (index >= profile().controls.size()) {
        return {};
    }
    const input::Control& found = profile().controls[index];
    return QString::fromStdString(found.name.empty() ? found.id : found.name);
}

QString ProfileEditor::conflictText(const std::string& path) const
{
    QStringList names;
    for (const input::InputConflict& conflict : m_conflicts) {
        if (std::ranges::none_of(conflict.bindings, [&](const auto& binding) { return binding.path == path; })) {
            continue;
        }
        for (const input::ConflictingBinding& binding : conflict.bindings) {
            const QString name = displayName(binding.index);
            if (binding.path != path && !name.isEmpty() && !names.contains(name)) {
                names.append(name);
            }
        }
    }
    if (names.isEmpty()) {
        return {};
    }
    return tr("Also bound to “%1”").arg(names.join(QStringLiteral("”, “")));
}

input::CoordinateMapper ProfileEditor::mapper() const
{
    return input::CoordinateMapper(m_screen, profile().screen.scaleMode, profile().screen.safeArea);
}

void ProfileEditor::placeAt(input::Control& control, input::Vec2 position) const
{
    const input::CoordinateMapper map = mapper();
    const input::Vec2 at = clamped(position);
    control.anchor = map.anchorAt(at);
    const input::Vec2 offset = map.offsetOf(control.anchor, at);
    control.offset = {fine(offset.x), fine(offset.y)};
}

std::vector<std::pair<std::size_t, input::Vec2>> ProfileEditor::selectedCenters(const input::Profile& profile) const
{
    const input::CoordinateMapper map = mapper();
    std::vector<std::pair<std::size_t, input::Vec2>> centers;
    for (std::size_t i = 0; i < profile.controls.size(); ++i) {
        const input::Control& control = profile.controls[i];
        if (m_selection.contains(QString::fromStdString(control.id))) {
            centers.emplace_back(i, map.position(control.anchor, control.offset));
        }
    }
    return centers;
}

std::string ProfileEditor::freeId(std::string_view base, const input::Profile& profile) const
{
    std::string stem(base);
    while (!stem.empty() && stem.back() >= '0' && stem.back() <= '9') {
        stem.pop_back();
    }
    if (stem.empty()) {
        stem = "control";
    }
    const auto taken = [&](const std::string& id) {
        return std::ranges::any_of(profile.controls, [&](const input::Control& c) { return c.id == id; }) ||
               std::ranges::any_of(profile.vars, [&](const input::Variable& v) { return v.id == id; });
    };
    if (!taken(stem)) {
        return stem;
    }
    for (int number = 2;; ++number) {
        std::string candidate = stem + std::to_string(number);
        if (!taken(candidate)) {
            return candidate;
        }
    }
}

std::string ProfileEditor::uniqueId(const std::string& wanted, const input::Profile& profile,
                                    std::string_view ignoring) const
{
    std::string stem = wanted;
    if (stem.empty()) {
        stem = "control";
    }
    if (stem.size() > input::kNameLength) {
        stem.resize(input::kNameLength);
    }
    const auto taken = [&](const std::string& id) {
        if (id == ignoring) {
            return false;
        }
        return std::ranges::any_of(profile.controls, [&](const input::Control& c) { return c.id == id; }) ||
               std::ranges::any_of(profile.vars, [&](const input::Variable& v) { return v.id == id; });
    };
    if (!taken(stem)) {
        return stem;
    }
    for (int number = 2;; ++number) {
        const std::string suffix = std::to_string(number);
        std::string candidate = stem;
        if (candidate.size() + suffix.size() > input::kNameLength) {
            candidate.resize(input::kNameLength - suffix.size());
        }
        candidate += suffix;
        if (!taken(candidate)) {
            return candidate;
        }
    }
}

std::string ProfileEditor::codeBaseOf(const input::Control& control) const
{
    if (const auto* placed = std::get_if<input::ComponentControl>(&control.kind)) {
        const std::string& ref = placed->component;
        const std::size_t slash = ref.rfind('/');
        const std::string base = slash == std::string::npos ? ref : ref.substr(slash + 1);
        return base.empty() ? std::string("component") : base;
    }
    if (const auto* behavior = std::get_if<input::BehaviorControl>(&control.kind)) {
        const std::string& ref = behavior->behavior;
        const std::size_t slash = ref.rfind('/');
        const std::string base = slash == std::string::npos ? ref : ref.substr(slash + 1);
        return base.empty() ? std::string("behavior") : base;
    }
    const std::string type(input::kControlTypeNames[control.kind.index()]);
    const auto found = std::ranges::find(recipes(), type, &Recipe::kind);
    return found != recipes().end() ? std::string(found->base) : stemOfId(control.id);
}

std::string ProfileEditor::keyPartOf(const input::Control& control) const
{
    const Json settings = Json::parse(input::controlSettings(control), nullptr, false);
    if (settings.is_discarded() || !settings.is_object()) {
        return {};
    }
    if (std::holds_alternative<input::WasdControl>(control.kind)) {
        std::string joined;
        for (const std::string_view slot : {"up", "left", "down", "right"}) {
            const std::optional<std::string> chord = chordIn(settings, slot, false);
            const std::string piece = chord ? keyPiece(*chord).toStdString() : std::string();
            if (piece.size() != 1) {
                joined.clear();
                break;
            }
            joined += piece;
        }
        if (!joined.empty()) {
            return joined;
        }
    }
    const std::vector<std::string_view> keys = keySlotsOf(control.kind);
    for (const std::string_view slot : primarySlotsOf(control.kind)) {
        if (slot == "lockKey" || std::ranges::find(keys, slot) == keys.end()) {
            continue;
        }
        const std::optional<std::string> chord = chordIn(settings, slot, false);
        if (chord && !chord->empty()) {
            return keyPiece(*chord).toStdString();
        }
    }
    return {};
}

std::vector<ProfileEditor::CodeRename> ProfileEditor::followKeys(input::Profile& next) const
{
    std::vector<CodeRename> renames;
    for (input::Control& control : next.controls) {
        if (control.idFixed || !control.parent.empty() || !control.component.empty()) {
            continue;
        }
        if (std::holds_alternative<input::ComponentControl>(control.kind) ||
            std::holds_alternative<input::BehaviorControl>(control.kind)) {
            continue;
        }
        const input::Control* before = controlById(profile(), control.id);
        if (before == nullptr || before->kind == control.kind) {
            continue;
        }
        const std::string part = keyPartOf(control);
        if (part.empty() || part == keyPartOf(*before)) {
            continue;
        }
        const std::string chosen = uniqueId(codeBaseOf(control) + part, next, control.id);
        if (chosen == control.id) {
            continue;
        }
        renames.push_back({control.id, chosen});
        control.id = chosen;
    }
    return renames;
}

void ProfileEditor::renameReferences(input::Profile& next, const std::vector<CodeRename>& renames) const
{
    const auto renamed = [&renames](const std::string& id) {
        const auto found = std::ranges::find_if(renames, [&id](const CodeRename& entry) { return entry.from == id; });
        return found != renames.end() ? found->to : id;
    };
    for (input::Control& control : next.controls) {
        control.id = renamed(control.id);
        if (!control.parent.empty()) {
            control.parent = renamed(control.parent);
        }
        if (auto* skill = std::get_if<input::SkillControl>(&control.kind); skill != nullptr) {
            skill->pauses = renamed(skill->pauses);
        }
        if (auto* wheel = std::get_if<input::ActionWheelControl>(&control.kind); wheel != nullptr) {
            for (input::WheelCell& cell : wheel->cells) {
                cell.control = renamed(cell.control);
            }
        }
        for (input::NamedValue& value : propsOf(control)) {
            auto* text = std::get_if<std::string>(&value.value);
            if (text == nullptr) {
                continue;
            }
            const ComponentEntry* entry = classOf(control);
            if (entry == nullptr) {
                continue;
            }
            const auto declared =
                std::ranges::find(entry->props, QString::fromStdString(value.name), &DeclaredSpec::name);
            if (declared != entry->props.end() && declared->kind == u"component") {
                *text = renamed(*text);
            }
        }
    }
}

void ProfileEditor::commit(input::Profile next, std::optional<QStringList> selection, std::vector<LogicEdit> logic,
                           bool follow)
{
    if (m_preview) {
        return;
    }
    m_lastEdit.reset();
    inDeclaredOrder(next);
    if (next == profile()) {
        return;
    }
    const QStringList before = m_selection;
    if (selection) {
        m_selection = std::move(*selection);
    }
    const std::vector<CodeRename> renames = follow ? followKeys(next) : std::vector<CodeRename>{};
    if (!renames.empty()) {
        renameReferences(next, renames);
        const auto renamedId = [&renames](const QString& id) {
            const std::string from = id.toStdString();
            const auto found = std::ranges::find_if(renames, [&from](const CodeRename& entry) { return entry.from == from; });
            return found != renames.end() ? QString::fromStdString(found->to) : id;
        };
        for (QString& chosen : m_selection) {
            chosen = renamedId(chosen);
        }
        QStringList handed = m_handedOver;
        for (QString& handedId : handed) {
            handedId = renamedId(handedId);
        }
        if (handed != m_handedOver) {
            m_handedOver = handed;
            emit handedOverChanged();
        }
        std::vector<LogicEdit> changed = changeLogic(renames, {});
        logic.insert(logic.end(), std::make_move_iterator(changed.begin()), std::make_move_iterator(changed.end()));
    }
    if (m_inGesture && m_gestureCommitted && logic.empty()) {
        m_history[m_at].profile = std::move(next);
        m_history[m_at].selectionAfter = m_selection;
    } else {
        m_history.resize(m_at + 1);
        m_history.push_back({.profile = std::move(next),
                             .selectionBefore = before,
                             .selectionAfter = m_selection,
                             .logic = std::move(logic)});
        ++m_at;
        if (m_history.size() > kMostSteps) {
            m_history.erase(m_history.begin());
            --m_at;
        }
        m_gestureCommitted = m_inGesture;
    }
    refresh(before);
}

void ProfileEditor::commitEdit(input::Profile next, const QString& id, const QString& key, bool mergeable)
{
    if (next == profile()) {
        return;
    }
    const std::pair<QString, QString> edit{id, key};
    if (!mergeable || m_lastEdit != edit || m_inGesture || m_at == 0) {
        commit(std::move(next));
        if (mergeable) {
            m_lastEdit = edit;
        }
        return;
    }
    const QStringList before = m_selection;
    if (next == m_history[m_at - 1].profile) {
        m_history.pop_back();
        --m_at;
        m_lastEdit.reset();
    } else {
        m_history[m_at].profile = std::move(next);
        m_history[m_at].selectionAfter = m_selection;
    }
    refresh(before);
}

QVariant ProfileEditor::lengthValue(double width) const
{
    return fine(mapper().sizeOf({width, width}).x);
}

QVariant ProfileEditor::areaValue(double width, double height) const
{
    return pairOf(mapper().sizeOf({width, height}));
}

QVariant ProfileEditor::pointValue(double x, double y) const
{
    return pairOf(mapper().offsetOf(input::Anchor::Center, clamped({x, y})));
}

void ProfileEditor::refresh(const QStringList& before)
{
    m_conflicts = input::inputConflicts(profile());
    keepSelectionValid();
    ++m_revision;
    if (before != m_selection) {
        emit selectionChanged();
    }
    emit changed();
}

void ProfileEditor::keepSelectionValid()
{
    m_selection.erase(std::remove_if(m_selection.begin(), m_selection.end(),
                                     [this](const QString& id) { return !indexOf(id); }),
                      m_selection.end());
}

void ProfileEditor::retranslate()
{
    ++m_revision;
    emit changed();
}

void ProfileEditor::markSavedAs(const QString& name)
{
    endGesture();
    const std::string title = name.toStdString();
    for (Step& step : m_history) {
        step.profile.meta.name = title;
    }
    markSaved();
}

void ProfileEditor::replaceSaved(input::Profile profile)
{
    endGesture();
    commit(std::move(profile), std::nullopt, {}, false);
    markSaved();
}

std::shared_ptr<const input::Profile> ProfileEditor::trial() const
{
    if (!m_trying) {
        return nullptr;
    }
    if (m_trial == nullptr || (m_trialRevision != m_revision && !(*m_trial == profile()))) {
        m_trial = std::make_shared<const input::Profile>(profile());
    }
    m_trialRevision = m_revision;
    return m_trial;
}

void ProfileEditor::setTrying(bool on)
{
    if (on == m_trying || (on && m_preview)) {
        return;
    }
    if (on && m_logicShown) {
        m_logicShown = false;
        emit logicShownChanged();
    }
    m_trying = on;
    emit tryingChanged();
}

QVariantList ProfileEditor::previewMarkers(double width, double height) const
{
    QVariantList items = markers(width, height);
    const input::CoordinateMapper geometry(input::ScreenGeometry{.width = width, .height = height},
                                           profile().screen.scaleMode, profile().screen.safeArea);
    for (QVariant& item : items) {
        QVariantMap map = item.toMap();
        const auto index = static_cast<std::size_t>(map.value(QStringLiteral("control")).toInt());
        if (!map.contains(QStringLiteral("path")) || index >= profile().controls.size()) {
            continue;
        }
        const input::Control& control = profile().controls[index];
        const auto* swipe = std::get_if<input::SwipeControl>(&control.kind);
        if (swipe == nullptr) {
            continue;
        }
        const input::Vec2 origin = geometry.position(control.anchor, control.offset);
        QVariantList points;
        for (const input::Vec2& point : swipe->path) {
            const input::Vec2 extent = geometry.extent(point);
            points.append(QVariantMap{{QStringLiteral("x"), (origin.x + extent.x) * width},
                                      {QStringLiteral("y"), (origin.y + extent.y) * height}});
        }
        map.insert(QStringLiteral("path"), points);
        item = map;
    }
    return items;
}

QStringList ProfileEditor::suggest(std::vector<Suggested> controls)
{
    QStringList ids;
    for (Suggested& suggested : controls) {
        Suggestion suggestion{.id = QStringLiteral("s%1").arg(++m_lastSuggestion),
                              .control = std::move(suggested.control),
                              .state = QStringLiteral("undecided")};
        suggestion.control.parent.clear();
        suggestion.control.child.clear();
        if (suggested.at) {
            placeAt(suggestion.control, *suggested.at);
        }
        ids.append(suggestion.id);
        m_suggestions.push_back(std::move(suggestion));
    }
    ++m_suggestionRevision;
    emit suggestionsChanged();
    return ids;
}

QString ProfileEditor::suggestionState(const QString& id) const
{
    const auto found = std::ranges::find(m_suggestions, id, &Suggestion::id);
    return found != m_suggestions.end() ? found->state : QString();
}

QVariantList ProfileEditor::suggestionMarkers(double width, double height) const
{
    input::Profile shown = profile();
    const std::size_t base = shown.controls.size();
    std::vector<const Suggestion*> pending;
    for (const Suggestion& suggestion : m_suggestions) {
        if (suggestion.state != u"undecided") {
            continue;
        }
        input::Control control = suggestion.control;
        control.id = "suggestion" + std::to_string(pending.size());
        shown.controls.push_back(std::move(control));
        pending.push_back(&suggestion);
    }
    QVariantList items;
    if (pending.empty() || width <= 0.0 || height <= 0.0) {
        return items;
    }
    const QVariantList hints = keyHintItems(shown, QSizeF(width, height), m_defaultLockKey, true);
    const input::CoordinateMapper geometry(input::ScreenGeometry{.width = width, .height = height},
                                           shown.screen.scaleMode, shown.screen.safeArea);
    for (std::size_t index = 0; index < pending.size(); ++index) {
        const Suggestion& suggestion = *pending[index];
        const input::Control& control = suggestion.control;
        QVariantMap item;
        for (const QVariant& hint : hints) {
            const QVariantMap map = hint.toMap();
            if (static_cast<std::size_t>(map.value(QStringLiteral("control")).toInt()) == base + index) {
                item = map;
                break;
            }
        }
        if (item.isEmpty()) {
            const input::Vec2 center = geometry.position(control.anchor, control.offset);
            item = QVariantMap{{QStringLiteral("x"), center.x * width},
                               {QStringLiteral("y"), center.y * height},
                               {QStringLiteral("rx"), 0.0},
                               {QStringLiteral("ry"), 0.0},
                               {QStringLiteral("keys"), QVariantList()}};
        }
        QStringList keys;
        for (const QVariant& key : item.value(QStringLiteral("keys")).toList()) {
            keys.append(key.toMap().value(QStringLiteral("text")).toString());
        }
        const QString type = nameOf(input::kControlTypeNames[control.kind.index()]);
        item.insert(QStringLiteral("id"), suggestion.id);
        item.insert(QStringLiteral("type"), type);
        item.insert(QStringLiteral("name"), control.name.empty() ? nameFor(type) : QString::fromStdString(control.name));
        item.insert(QStringLiteral("keyText"), keys.join(u' '));
        items.append(item);
    }
    return items;
}

bool ProfileEditor::acceptSuggestion(const QString& id)
{
    const auto found = std::ranges::find(m_suggestions, id, &Suggestion::id);
    if (found == m_suggestions.end() || found->state != u"undecided") {
        return false;
    }
    input::Profile next = profile();
    input::Control control = found->control;
    const QString type = nameOf(input::kControlTypeNames[control.kind.index()]);
    control.id = freeId(control.id.empty() ? type.toStdString() : control.id, next);
    const QString wanted = control.name.empty() ? nameFor(type) : QString::fromStdString(control.name);
    control.name = numberedName(wanted, next, true);
    if (auto* camera = std::get_if<input::CameraControl>(&control.kind); camera != nullptr && !camera->lockKey) {
        camera->lockKey = m_defaultLockKey;
    }
    const QString made = QString::fromStdString(control.id);
    next.controls.push_back(std::move(control));
    found->state = QStringLiteral("accepted");
    ++m_suggestionRevision;
    commit(std::move(next), QStringList{made});
    emit suggestionsChanged();
    emit suggestionDecided(id, QStringLiteral("accepted"), made);
    return true;
}

bool ProfileEditor::rejectSuggestion(const QString& id)
{
    const auto found = std::ranges::find(m_suggestions, id, &Suggestion::id);
    if (found == m_suggestions.end() || found->state != u"undecided") {
        return false;
    }
    found->state = QStringLiteral("rejected");
    ++m_suggestionRevision;
    emit suggestionsChanged();
    emit suggestionDecided(id, QStringLiteral("rejected"), QString());
    return true;
}

bool ProfileEditor::placeSuggestion(const QString& id, double x, double y)
{
    const auto found = std::ranges::find(m_suggestions, id, &Suggestion::id);
    if (found == m_suggestions.end() || found->state != u"undecided" || !std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    placeAt(found->control, {x, y});
    ++m_suggestionRevision;
    emit suggestionsChanged();
    return true;
}

std::shared_ptr<const input::Profile> profileInUse(const ProfileEditor* editor,
                                                   std::shared_ptr<const input::Profile> saved)
{
    if (std::shared_ptr<const input::Profile> trial = editor != nullptr ? editor->trial() : nullptr) {
        return trial;
    }
    return saved;
}

namespace {

QString qstringOf(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::optional<DeclaredModule> moduleOf(std::string_view module)
{
    const NativeExtensions* native = NativeExtensions::instance();
    return native != nullptr ? native->declaredModule(module) : std::nullopt;
}

int decimalsOf(double step)
{
    if (!(step > 0.0) || step >= 1.0) {
        return 0;
    }
    return std::clamp(static_cast<int>(std::ceil(-std::log10(step) - 1e-9)), 0, 4);
}

std::string chosenText(const ext::DeclaredParam& param, const Json& chosen)
{
    if (chosen.is_object()) {
        if (const auto found = chosen.find(param.key); found != chosen.end()) {
            std::string text = found->dump(-1, ' ', false, Json::error_handler_t::replace);
            if (ext::declaredProblem(param, text).empty()) {
                return text;
            }
        }
    }
    return ext::declaredDefault(param);
}

Json chosenOf(const std::string& params)
{
    return params.empty() ? Json::object() : Json::parse(params, nullptr, false);
}

QVariantMap paramMap(const ext::DeclaredParam& param, const NativeTexts& texts, std::size_t at, const Json& chosen)
{
    const std::string text = chosenText(param, chosen);
    const Json value = Json::parse(text, nullptr, false);
    const bool whole = param.type == ext::DeclaredParamType::Integer;
    const bool bounded = param.min.has_value() && param.max.has_value();
    const double low = param.min.value_or(-1000000.0);
    const double high = param.max.value_or(1000000.0);
    const double step = param.step.value_or(whole ? 1.0 : bounded ? (high - low) / 100.0 : 0.01);
    QVariantList options;
    for (std::size_t option = 0; option < param.options.size(); ++option) {
        const bool named = at < texts.paramOptions.size() && option < texts.paramOptions[at].size();
        options.append(QVariantMap{
            {QStringLiteral("value"), qstringOf(param.options[option])},
            {QStringLiteral("text"), named ? texts.paramOptions[at][option] : qstringOf(param.options[option])}});
    }
    QVariantMap map{{QStringLiteral("key"), qstringOf(param.key)},
                    {QStringLiteral("type"), qstringOf(ext::kParamTypeNames[static_cast<std::size_t>(param.type)])},
                    {QStringLiteral("title"), at < texts.paramTitles.size() ? texts.paramTitles[at] : qstringOf(param.key)},
                    {QStringLiteral("description"),
                     at < texts.paramDescriptions.size() ? texts.paramDescriptions[at] : QString()},
                    {QStringLiteral("json"), qstringOf(text)},
                    {QStringLiteral("value"), toVariant(value)},
                    {QStringLiteral("bounded"), bounded},
                    {QStringLiteral("minimum"), low},
                    {QStringLiteral("maximum"), high},
                    {QStringLiteral("step"), step},
                    {QStringLiteral("decimals"), whole ? 0 : decimalsOf(step)},
                    {QStringLiteral("logarithmic"), param.log && bounded && low > 0.0},
                    {QStringLiteral("unit"), qstringOf(param.unit)},
                    {QStringLiteral("shape"), qstringOf(ext::kRegionShapeNames[static_cast<std::size_t>(param.shape)])},
                    {QStringLiteral("options"), options}};
    if (param.type == ext::DeclaredParamType::Key) {
        map.insert(QStringLiteral("keys"),
                   capsOf(value.is_string() ? input::parseChord(value.get<std::string>()) : std::nullopt));
    }
    return map;
}

QVariantList paramMaps(const DeclaredModule& declared, const std::string& params)
{
    const Json chosen = chosenOf(params);
    QVariantList maps;
    for (std::size_t at = 0; at < declared.params.size(); ++at) {
        maps.append(paramMap(declared.params[at], declared.texts, at, chosen));
    }
    return maps;
}

std::vector<input::GlobalAttachment> globalsFor(const input::Control& control)
{
    std::vector<input::GlobalAttachment> found;
    const NativeExtensions* native = NativeExtensions::instance();
    const std::shared_ptr<ExtensionModules> modules = native != nullptr ? native->moduleFactory() : nullptr;
    if (modules == nullptr) {
        return found;
    }
    input::Control open = control;
    open.skipGlobal.clear();
    for (const input::GlobalAttachment& global : modules->globals()) {
        if (input::takesGlobal(open, global)) {
            found.push_back(global);
        }
    }
    return found;
}

}

QVariantList declaredParamMaps(const DeclaredModule& declared, const std::string& params)
{
    return paramMaps(declared, params);
}

QVariantMap ProfileEditor::addBehavior(const QString& module, double x, double y)
{
    const std::optional<DeclaredModule> declared = moduleOf(module.toStdString());
    if (!declared || declared->kind != exthost::InstanceKind::Behavior || !input::isModuleRef(declared->module)) {
        return {};
    }
    input::Profile next = profile();
    input::Control control;
    control.id = freeId(declared->module.substr(declared->module.rfind('/') + 1), next);
    control.name = numberedName(declared->texts.title, next, true);
    control.kind = input::BehaviorControl{.behavior = declared->module, .inputs = {}, .params = {}};
    placeAt(control, {x, y});
    const QString id = QString::fromStdString(control.id);
    next.controls.push_back(std::move(control));
    QString slot;
    for (const ext::DeclaredSlot& wanted : declared->slotList) {
        if (wanted.kind != ext::NativeSlotKind::Axis2) {
            slot = QStringLiteral("inputs.") + qstringOf(wanted.name);
            break;
        }
    }
    commit(std::move(next), QStringList{id});
    return {{QStringLiteral("id"), id}, {QStringLiteral("slot"), slot}};
}

QVariantList ProfileEditor::attachmentsOf(const QString& id) const
{
    const auto at = indexOf(id);
    if (!at) {
        return {};
    }
    const input::Control& control = profile().controls[*at];
    const std::string_view type = input::kControlTypeNames[control.kind.index()];
    QVariantList list;
    for (const input::GlobalAttachment& global : globalsFor(control)) {
        const std::optional<DeclaredModule> declared = moduleOf(global.module);
        const bool skipped = std::ranges::any_of(
            control.skipGlobal, [&global](const input::SkippedGlobal& skip) { return skip.module == global.module; });
        list.append(QVariantMap{
            {QStringLiteral("module"), qstringOf(global.module)},
            {QStringLiteral("title"), declared ? declared->texts.title : qstringOf(global.module)},
            {QStringLiteral("extension"),
             declared ? declared->extensionName : qstringOf(input::moduleExtension(global.module))},
            {QStringLiteral("enabled"), !skipped},
            {QStringLiteral("available"), true},
            {QStringLiteral("fits"), true},
            {QStringLiteral("global"), true},
            {QStringLiteral("place"), -1},
            {QStringLiteral("params"), QVariantList()}});
    }
    for (std::size_t place = 0; place < control.attachments.size(); ++place) {
        const input::Attachment& attachment = control.attachments[place];
        const std::optional<DeclaredModule> declared = moduleOf(attachment.module);
        const bool available = declared && declared->kind == exthost::InstanceKind::Attachment;
        list.append(QVariantMap{
            {QStringLiteral("module"), qstringOf(attachment.module)},
            {QStringLiteral("title"), available ? declared->texts.title : qstringOf(attachment.module)},
            {QStringLiteral("extension"),
             available ? declared->extensionName : qstringOf(input::moduleExtension(attachment.module))},
            {QStringLiteral("enabled"), attachment.enabled},
            {QStringLiteral("available"), available},
            {QStringLiteral("fits"), available && moduleFits(declared->appliesTo, type)},
            {QStringLiteral("global"), false},
            {QStringLiteral("place"), static_cast<int>(place)},
            {QStringLiteral("params"), available ? paramMaps(*declared, attachment.params) : QVariantList()}});
    }
    return list;
}

bool ProfileEditor::skipGlobal(const QString& id, const QString& module, bool skip)
{
    const auto at = indexOf(id);
    const std::string wanted = module.toStdString();
    if (!at || !input::isModuleRef(wanted)) {
        return false;
    }
    const std::vector<input::SkippedGlobal>& current = profile().controls[*at].skipGlobal;
    const auto found = std::ranges::find(current, wanted, &input::SkippedGlobal::module);
    if (skip == (found != current.end()) || (skip && current.size() >= input::kMostSkippedGlobals)) {
        return false;
    }
    input::Profile next = profile();
    std::vector<input::SkippedGlobal>& list = next.controls[*at].skipGlobal;
    if (skip) {
        list.push_back({.module = wanted});
    } else {
        std::erase_if(list, [&wanted](const input::SkippedGlobal& entry) { return entry.module == wanted; });
    }
    commit(std::move(next));
    return true;
}

QVariantList ProfileEditor::attachable(const QString& id) const
{
    const auto at = indexOf(id);
    const NativeExtensions* native = NativeExtensions::instance();
    if (!at || native == nullptr) {
        return {};
    }
    const std::string_view type = input::kControlTypeNames[profile().controls[*at].kind.index()];
    QVariantList offers;
    for (const DeclaredModule& declared : native->declaredModules()) {
        if (declared.kind != exthost::InstanceKind::Attachment || !moduleFits(declared.appliesTo, type)) {
            continue;
        }
        offers.append(QVariantMap{{QStringLiteral("module"), qstringOf(declared.module)},
                                  {QStringLiteral("title"), declared.texts.title},
                                  {QStringLiteral("description"), declared.texts.description},
                                  {QStringLiteral("extension"), declared.extensionName}});
    }
    return offers;
}

bool ProfileEditor::addAttachment(const QString& id, const QString& module)
{
    const auto at = indexOf(id);
    const std::string wanted = module.toStdString();
    if (!at || !input::isModuleRef(wanted) || profile().controls[*at].attachments.size() >= input::kMostAttachments) {
        return false;
    }
    const std::optional<DeclaredModule> declared = moduleOf(wanted);
    if (!declared || declared->kind != exthost::InstanceKind::Attachment ||
        !moduleFits(declared->appliesTo, input::kControlTypeNames[profile().controls[*at].kind.index()])) {
        return false;
    }
    input::Profile next = profile();
    next.controls[*at].attachments.push_back({.module = wanted, .enabled = true, .params = {}});
    commit(std::move(next));
    return true;
}

bool ProfileEditor::removeAttachment(const QString& id, int index)
{
    const auto at = indexOf(id);
    if (!at || index < 0 || static_cast<std::size_t>(index) >= profile().controls[*at].attachments.size()) {
        return false;
    }
    input::Profile next = profile();
    std::vector<input::Attachment>& attachments = next.controls[*at].attachments;
    attachments.erase(attachments.begin() + index);
    commit(std::move(next));
    return true;
}

bool ProfileEditor::moveAttachment(const QString& id, int index, int step)
{
    const auto at = indexOf(id);
    const int target = index + step;
    if (!at || index < 0 || target < 0) {
        return false;
    }
    const std::size_t count = profile().controls[*at].attachments.size();
    if (static_cast<std::size_t>(index) >= count || static_cast<std::size_t>(target) >= count || target == index) {
        return false;
    }
    input::Profile next = profile();
    std::vector<input::Attachment>& attachments = next.controls[*at].attachments;
    std::swap(attachments[static_cast<std::size_t>(index)], attachments[static_cast<std::size_t>(target)]);
    commit(std::move(next));
    return true;
}

bool ProfileEditor::enableAttachment(const QString& id, int index, bool on)
{
    const auto at = indexOf(id);
    if (!at || index < 0 || static_cast<std::size_t>(index) >= profile().controls[*at].attachments.size()) {
        return false;
    }
    input::Profile next = profile();
    next.controls[*at].attachments[static_cast<std::size_t>(index)].enabled = on;
    commit(std::move(next));
    return true;
}

QVariantList ProfileEditor::behaviorParams(const QString& id) const
{
    const auto at = indexOf(id);
    const auto* behavior = at ? std::get_if<input::BehaviorControl>(&profile().controls[*at].kind) : nullptr;
    if (behavior == nullptr) {
        return {};
    }
    const std::optional<DeclaredModule> declared = moduleOf(behavior->behavior);
    if (!declared || declared->kind != exthost::InstanceKind::Behavior) {
        return {};
    }
    return paramMaps(*declared, behavior->params);
}

bool ProfileEditor::setModuleParam(const QString& id, int attachment, const QString& key, const QString& json)
{
    const auto at = indexOf(id);
    if (!at || key.isEmpty() || key.contains(u'.')) {
        return false;
    }
    const input::Control& control = profile().controls[*at];
    std::string module;
    QString path;
    if (attachment < 0) {
        const auto* behavior = std::get_if<input::BehaviorControl>(&control.kind);
        if (behavior == nullptr) {
            return false;
        }
        module = behavior->behavior;
        path = QStringLiteral("params.") + key;
    } else {
        if (static_cast<std::size_t>(attachment) >= control.attachments.size()) {
            return false;
        }
        module = control.attachments[static_cast<std::size_t>(attachment)].module;
        path = QStringLiteral("attachments.%1.params.").arg(attachment) + key;
    }
    const Json value = Json::parse(json.toStdString(), nullptr, false);
    if (value.is_discarded() || value.is_null()) {
        return false;
    }
    if (const std::optional<DeclaredModule> declared = moduleOf(module)) {
        const auto param = std::ranges::find(declared->params, key.toStdString(), &ext::DeclaredParam::key);
        if (param == declared->params.end() || !ext::declaredProblem(*param, value.dump()).empty()) {
            return false;
        }
    }
    std::optional<input::Profile> next = withValue(profile(), *at, path, toVariant(value));
    if (!next) {
        return false;
    }
    commitEdit(std::move(*next), id, QStringLiteral("module:") + path, value.is_number());
    return true;
}

QVariantList ProfileEditor::behaviorFields(std::size_t index) const
{
    const input::Control& control = profile().controls[index];
    const auto* behavior = std::get_if<input::BehaviorControl>(&control.kind);
    if (behavior == nullptr) {
        return {};
    }
    const std::optional<DeclaredModule> declared = moduleOf(behavior->behavior);
    if (!declared || declared->kind != exthost::InstanceKind::Behavior) {
        return {QVariantMap{{QStringLiteral("key"), QStringLiteral("behaviorMissing")},
                            {QStringLiteral("widget"), QStringLiteral("note")},
                            {QStringLiteral("title"), QString()},
                            {QStringLiteral("hint"), QString()},
                            {QStringLiteral("half"), false},
                            {QStringLiteral("enabled"), true},
                            {QStringLiteral("value"),
                             tr("Needs the extension %1, which is not installed or is off")
                                 .arg(qstringOf(input::moduleExtension(behavior->behavior)))}}};
    }
    const QVariantList sticks{
        QVariantMap{{QStringLiteral("value"), QString()}, {QStringLiteral("text"), tr("Not bound")}},
        QVariantMap{{QStringLiteral("value"), QStringLiteral("Mouse.Move")}, {QStringLiteral("text"), tr("Mouse movement")}},
        QVariantMap{{QStringLiteral("value"), QStringLiteral("Gamepad.LeftStick")}, {QStringLiteral("text"), tr("Left stick")}},
        QVariantMap{{QStringLiteral("value"), QStringLiteral("Gamepad.RightStick")},
                    {QStringLiteral("text"), tr("Right stick")}}};
    const std::string section = "$.controls[" + std::to_string(index) + "].";
    QVariantList fields;
    for (std::size_t slot = 0; slot < declared->slotList.size(); ++slot) {
        const ext::DeclaredSlot& wanted = declared->slotList[slot];
        const QString name = qstringOf(wanted.name);
        const QString path = QStringLiteral("inputs.") + name;
        const auto bound = std::ranges::find(behavior->inputs, wanted.name, &input::NamedBinding::name);
        const std::optional<input::Chord> chord =
            bound != behavior->inputs.end() && bound->binding ? bound->binding->input : std::nullopt;
        QVariantMap field{{QStringLiteral("key"), QStringLiteral("slot:") + name},
                          {QStringLiteral("title"),
                           slot < declared->texts.slotTitles.size() ? declared->texts.slotTitles[slot] : name},
                          {QStringLiteral("hint"), QString()},
                          {QStringLiteral("half"), false},
                          {QStringLiteral("enabled"), true}};
        if (wanted.kind == ext::NativeSlotKind::Axis2) {
            field.insert(QStringLiteral("widget"), QStringLiteral("select"));
            field.insert(QStringLiteral("options"), sticks);
            field.insert(QStringLiteral("value"),
                         chord ? QString::fromStdString(input::chordName(*chord)) : QString());
        } else {
            field.insert(QStringLiteral("widget"), QStringLiteral("key"));
            field.insert(QStringLiteral("slot"), path);
            field.insert(QStringLiteral("keys"), capsOf(chord));
            field.insert(QStringLiteral("bindPath"), path);
            field.insert(QStringLiteral("clearPath"), path);
            field.insert(QStringLiteral("conflict"), chord ? conflictText(section + path.toStdString()) : QString());
        }
        fields.append(field);
    }
    return fields;
}

QVariantList ProfileEditor::modulePoints(std::size_t index, double width, double height) const
{
    const NativeExtensions* native = NativeExtensions::instance();
    if (native == nullptr || index >= profile().controls.size()) {
        return {};
    }
    const input::Control& control = profile().controls[index];
    const auto* behavior = std::get_if<input::BehaviorControl>(&control.kind);
    if (behavior == nullptr && control.attachments.empty()) {
        return {};
    }
    const input::CoordinateMapper geometry = mapper();
    const input::Vec2 origin = geometry.position(control.anchor, control.offset);
    QVariantList points;
    const auto collect = [&](const DeclaredModule& declared, const std::string& params, int place) {
        const Json chosen = chosenOf(params);
        for (const ext::DeclaredParam& param : declared.params) {
            if (param.type != ext::DeclaredParamType::Point && param.type != ext::DeclaredParamType::Region) {
                continue;
            }
            const Json value = Json::parse(chosenText(param, chosen), nullptr, false);
            if (!value.is_array() || value.size() < 2 || !value[0].is_number() || !value[1].is_number()) {
                continue;
            }
            const input::Vec2 step = geometry.extent({value[0].get<double>(), value[1].get<double>()});
            points.append(QVariantMap{{QStringLiteral("name"), QStringLiteral("module:%1:").arg(place) + qstringOf(param.key)},
                                      {QStringLiteral("x"), (origin.x + step.x) * width},
                                      {QStringLiteral("y"), (origin.y + step.y) * height}});
        }
    };
    if (behavior != nullptr) {
        if (const std::optional<DeclaredModule> declared = native->declaredModule(behavior->behavior);
            declared && declared->kind == exthost::InstanceKind::Behavior) {
            collect(*declared, behavior->params, -1);
        }
    }
    for (std::size_t place = 0; place < control.attachments.size(); ++place) {
        const input::Attachment& attachment = control.attachments[place];
        if (!attachment.enabled) {
            continue;
        }
        if (const std::optional<DeclaredModule> declared = native->declaredModule(attachment.module);
            declared && declared->kind == exthost::InstanceKind::Attachment) {
            collect(*declared, attachment.params, static_cast<int>(place));
        }
    }
    return points;
}

bool ProfileEditor::setBehaviorInput(const QString& id, const QString& slot, const QString& chord)
{
    const auto at = indexOf(id);
    if (!at || slot.isEmpty() || slot.contains(u'.') ||
        !std::holds_alternative<input::BehaviorControl>(profile().controls[*at].kind)) {
        return false;
    }
    const QString path = QStringLiteral("inputs.") + slot;
    std::optional<input::Profile> next =
        chord.isEmpty() ? withValue(profile(), *at, path, QVariant()) : withInput(profile(), *at, path, chord);
    if (!next) {
        return false;
    }
    commit(std::move(*next));
    return true;
}

bool ProfileEditor::setModulePoint(const QString& id, const QString& name, double x, double y)
{
    const QStringList parts = name.split(u':');
    const auto at = indexOf(id);
    bool read = false;
    const int place = parts.size() == 3 ? parts[1].toInt(&read) : 0;
    if (!at || !read || parts[2].isEmpty() || !std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    const input::Control& control = profile().controls[*at];
    std::string module;
    std::string params;
    if (place < 0) {
        const auto* behavior = std::get_if<input::BehaviorControl>(&control.kind);
        if (behavior == nullptr) {
            return false;
        }
        module = behavior->behavior;
        params = behavior->params;
    } else {
        if (static_cast<std::size_t>(place) >= control.attachments.size()) {
            return false;
        }
        module = control.attachments[static_cast<std::size_t>(place)].module;
        params = control.attachments[static_cast<std::size_t>(place)].params;
    }
    const std::optional<DeclaredModule> declared = moduleOf(module);
    if (!declared) {
        return false;
    }
    const auto param = std::ranges::find(declared->params, parts[2].toStdString(), &ext::DeclaredParam::key);
    if (param == declared->params.end() ||
        (param->type != ext::DeclaredParamType::Point && param->type != ext::DeclaredParamType::Region)) {
        return false;
    }
    Json value = Json::parse(chosenText(*param, chosenOf(params)), nullptr, false);
    if (!value.is_array() || value.size() < 2) {
        return false;
    }
    const input::CoordinateMapper geometry = mapper();
    const input::Vec2 origin = geometry.position(control.anchor, control.offset);
    const input::Vec2 point = clamped({x, y});
    const input::Vec2 step = geometry.sizeOf({point.x - origin.x, point.y - origin.y});
    value[0] = std::clamp(fine(step.x), -2.0, 2.0);
    value[1] = std::clamp(fine(step.y), -2.0, 2.0);
    return setModuleParam(id, place, parts[2], QString::fromStdString(value.dump()));
}

}
