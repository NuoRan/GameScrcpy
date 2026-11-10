#include "ExtensionPanels.h"

#include "AppSettings.h"
#include "ExtensionHost.h"
#include "LogicServices.h"
#include "QtConversions.h"
#include "ThemeTokens.h"

#include <gs/ext/Archive.h>

#include <QFile>
#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaObject>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;

constexpr std::string_view kTreeCode = "GS-API-075";
constexpr std::string_view kPanelCode = "GS-API-076";
constexpr int kMostJsonNesting = 48;
constexpr std::size_t kMostIdLength = 64;
constexpr std::size_t kMostTitleLength = 200;
constexpr std::size_t kMostButtonTitleLength = 60;
constexpr std::size_t kMostTextLength = 4000;
constexpr std::size_t kMostStatusTextLength = 100;
constexpr std::size_t kMostUnitLength = 16;
constexpr std::size_t kMostIconLength = 64;
constexpr std::size_t kMostCellLength = 200;
constexpr std::size_t kMostColumnLength = 60;
constexpr std::size_t kMostMenuTitleLength = 100;
constexpr std::size_t kMostListItems = 500;
constexpr std::size_t kMostOptions = 64;
constexpr std::size_t kMostColumns = 16;
constexpr std::size_t kMostRows = 200;
constexpr std::size_t kMostSeries = 4;
constexpr std::size_t kMostPoints = 500;
constexpr std::size_t kMostShapes = 500;
constexpr std::size_t kMostPolylineNumbers = 1000;
constexpr double kMostSide = 2000.0;
constexpr double kLeastChartHeight = 60.0;
constexpr double kMostChartHeight = 480.0;
constexpr double kMostStrokeWidth = 20.0;

constexpr std::array<std::string_view, 4> kServices{"ui.render", "ui.status", "ui.menu", "ui.close"};
constexpr std::array<std::string_view, 6> kEventTypes{"click", "change", "submit", "select", "open", "close"};
constexpr std::array<std::string_view, 6> kTones{"normal", "muted", "accent", "good", "warn", "bad"};
constexpr std::array<std::string_view, 2> kFormats{"plain", "markdown"};
constexpr std::array<std::string_view, 3> kTextStyles{"body", "heading", "caption"};
constexpr std::array<std::string_view, 3> kButtonStyles{"normal", "primary", "danger"};
constexpr std::array<std::string_view, 2> kChartKinds{"line", "bar"};
constexpr std::array<std::string_view, 5> kShapeKinds{"rect", "circle", "line", "polyline", "text"};
constexpr std::array<std::string_view, 3> kShapeSizes{"body", "caption", "heading"};

bool nestingWithin(std::string_view text, int most) noexcept
{
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (const char c : text) {
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
        } else if (c == '"') {
            inString = true;
        } else if (c == '{' || c == '[') {
            if (++depth > most) {
                return false;
            }
        } else if (c == '}' || c == ']') {
            --depth;
        }
    }
    return true;
}

std::size_t characters(std::string_view text) noexcept
{
    return static_cast<std::size_t>(
        std::ranges::count_if(text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

template <std::size_t N>
bool oneOf(const std::array<std::string_view, N>& names, std::string_view value)
{
    return std::ranges::find(names, value) != names.end();
}

template <std::size_t N>
std::string listed(const std::array<std::string_view, N>& names)
{
    std::string text;
    for (std::size_t index = 0; index < N; ++index) {
        text += index == 0 ? "" : index + 1 == N ? " or " : ", ";
        text += names[index];
    }
    return text;
}

std::string shortened(std::string_view text)
{
    constexpr std::size_t kMost = 40;
    if (text.size() <= kMost) {
        return std::string(text);
    }
    std::size_t end = kMost;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
        --end;
    }
    return std::string(text.substr(0, end)) + "\u2026";
}

std::string member(const std::string& path, std::string_view field)
{
    return path + "." + std::string(field);
}

std::string indexed(const std::string& path, std::size_t index)
{
    return path + "[" + std::to_string(index) + "]";
}

bool hexColor(std::string_view text) noexcept
{
    return text.size() == 7 && text.front() == '#' && std::all_of(text.begin() + 1, text.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
           });
}

bool knownColor(std::string_view text)
{
    return hexColor(text) || text == "accent" || oneOf(kThemeTokenNames, text);
}

QString numberText(double value)
{
    if (std::trunc(value) == value && std::abs(value) < 1e15) {
        return QString::number(static_cast<long long>(value));
    }
    return QString::number(value, 'g', 12);
}

bool inside(const fs::path& root, const fs::path& file)
{
    auto filePart = file.begin();
    for (auto rootPart = root.begin(); rootPart != root.end(); ++rootPart, ++filePart) {
        if (filePart == file.end() || *filePart != *rootPart) {
            return false;
        }
    }
    return filePart != file.end();
}

script::ServiceAnswer refused(std::string_view code, std::string problem)
{
    return {"null", std::string(code), std::move(problem)};
}

class Reader
{
public:
    Reader(fs::path folder, std::size_t mostNodes, bool statusOnly)
        : m_folder(std::move(folder))
        , m_mostNodes(mostNodes)
        , m_statusOnly(statusOnly)
    {
    }

    [[nodiscard]] bool ok() const noexcept { return m_problem.empty(); }
    [[nodiscard]] const std::string& problem() const noexcept { return m_problem; }

    bool fail(const std::string& what, const std::string& path)
    {
        if (m_problem.empty()) {
            m_problem = what + " at " + path;
        }
        return false;
    }

    QVariant readNode(const Json& json, const std::string& path, std::size_t depth)
    {
        if (depth > kMostUiDepth) {
            fail("the tree is deeper than " + std::to_string(kMostUiDepth) + " levels", path);
            return {};
        }
        if (!json.is_object()) {
            fail("a node must be an object", path);
            return {};
        }
        if (++m_nodes > m_mostNodes) {
            fail("there are more than " + std::to_string(m_mostNodes) + " nodes", path);
            return {};
        }
        const auto type = json.find("type");
        if (type == json.end() || !type->is_string()) {
            fail("a node needs a type", path);
            return {};
        }
        const std::string& kind = type->get_ref<const std::string&>();
        QVariantMap out{{QStringLiteral("type"), toQString(kind)}};
        QString id;
        if (!readText(json, "id", path, kMostIdLength, id)) {
            return {};
        }
        out.insert(QStringLiteral("id"), id);
        const bool done = m_statusOnly ? statusNode(kind, json, path, depth, out) : panelNode(kind, json, path, depth, out);
        return done ? QVariant(out) : QVariant();
    }

    bool readText(const Json& object, const char* field, const std::string& path, std::size_t most, QString& out,
                  bool required = false)
    {
        const auto found = object.find(field);
        if (found == object.end() || found->is_null()) {
            return !required || fail(std::string(field) + " is missing", path);
        }
        if (!found->is_string()) {
            return fail(std::string(field) + " must be text", member(path, field));
        }
        const std::string& written = found->get_ref<const std::string&>();
        if (characters(written) > most) {
            return fail(std::string(field) + " is longer than " + std::to_string(most) + " characters", member(path, field));
        }
        if (required && written.empty()) {
            return fail(std::string(field) + " is empty", member(path, field));
        }
        out = toQString(written);
        return true;
    }

    bool readNumber(const Json& object, const char* field, const std::string& path, std::optional<double>& out)
    {
        const auto found = object.find(field);
        if (found == object.end() || found->is_null()) {
            return true;
        }
        if (!found->is_number()) {
            return fail(std::string(field) + " must be a number", member(path, field));
        }
        const double value = found->get<double>();
        if (!std::isfinite(value)) {
            return fail(std::string(field) + " must be a finite number", member(path, field));
        }
        out = value;
        return true;
    }

    bool readNeeded(const Json& object, const char* field, const std::string& path, double& out)
    {
        std::optional<double> value;
        if (!readNumber(object, field, path, value)) {
            return false;
        }
        if (!value) {
            return fail(std::string(field) + " is missing", path);
        }
        out = *value;
        return true;
    }

    bool readFlag(const Json& object, const char* field, const std::string& path, bool& out)
    {
        const auto found = object.find(field);
        if (found == object.end() || found->is_null()) {
            return true;
        }
        if (!found->is_boolean()) {
            return fail(std::string(field) + " must be true or false", member(path, field));
        }
        out = found->get<bool>();
        return true;
    }

    template <std::size_t N>
    bool readChoice(const Json& object, const char* field, const std::string& path,
                    const std::array<std::string_view, N>& names, QString& out)
    {
        out = toQString(names.front());
        const auto found = object.find(field);
        if (found == object.end() || found->is_null()) {
            return true;
        }
        if (!found->is_string() || !oneOf(names, found->get_ref<const std::string&>())) {
            return fail(std::string(field) + " must be " + listed(names), member(path, field));
        }
        out = toQString(found->get_ref<const std::string&>());
        return true;
    }

    bool readColor(const Json& object, const char* field, const std::string& path, QString& out)
    {
        const auto found = object.find(field);
        if (found == object.end() || found->is_null()) {
            return true;
        }
        if (!found->is_string() || !knownColor(found->get_ref<const std::string&>())) {
            return fail(std::string(field) + " must be a color written #RRGGBB or the name of a theme color",
                        member(path, field));
        }
        out = toQString(found->get_ref<const std::string&>());
        return true;
    }

    const Json* readArray(const Json& object, const char* field, const std::string& path, std::size_t most,
                          bool required)
    {
        const auto found = object.find(field);
        if (found == object.end() || found->is_null()) {
            if (required) {
                fail(std::string(field) + " is missing", path);
            }
            return nullptr;
        }
        if (!found->is_array()) {
            fail(std::string(field) + " must be a list", member(path, field));
            return nullptr;
        }
        if (found->size() > most) {
            fail(std::string(field) + " has more than " + std::to_string(most) + " items", member(path, field));
            return nullptr;
        }
        return &*found;
    }

private:
    bool needsId(const QVariantMap& out, std::string_view kind, const std::string& path)
    {
        return !out.value(QStringLiteral("id")).toString().isEmpty() ||
               fail("a " + std::string(kind) + " needs an id, so that its events say which one it was", path);
    }

    bool panelNode(const std::string& kind, const Json& json, const std::string& path, std::size_t depth,
                   QVariantMap& out)
    {
        if (kind == "column" || kind == "row" || kind == "group") {
            return containerNode(kind, json, path, depth, out);
        }
        if (kind == "text") {
            return textNode(json, path, kMostTextLength, out);
        }
        if (kind == "button") {
            return buttonNode(json, path, out);
        }
        if (kind == "field") {
            return fieldNode(json, path, out);
        }
        if (kind == "number") {
            return numberNode(json, path, out);
        }
        if (kind == "toggle") {
            return toggleNode(json, path, out);
        }
        if (kind == "choice") {
            return choiceNode(json, path, out);
        }
        if (kind == "slider") {
            return sliderNode(json, path, out);
        }
        if (kind == "list") {
            return listNode(json, path, out);
        }
        if (kind == "table") {
            return tableNode(json, path, out);
        }
        if (kind == "chart") {
            return chartNode(json, path, out);
        }
        if (kind == "canvas") {
            return canvasNode(json, path, out);
        }
        if (kind == "image") {
            return imageNode(json, path, out);
        }
        if (kind == "progress") {
            return progressNode(json, path, out);
        }
        if (kind == "separator") {
            return true;
        }
        return fail("\"" + shortened(kind) + "\" is not a node type", member(path, "type"));
    }

    bool statusNode(const std::string& kind, const Json& json, const std::string& path, std::size_t depth,
                    QVariantMap& out)
    {
        if (kind == "row") {
            if (depth > 1) {
                return fail("a row in a status widget cannot hold another row", path);
            }
            return containerNode(kind, json, path, depth, out);
        }
        if (kind == "text") {
            return textNode(json, path, kMostStatusTextLength, out);
        }
        if (kind == "icon") {
            QString name;
            QString tone;
            if (!readText(json, "name", path, kMostIconLength, name, true) || !readChoice(json, "tone", path, kTones, tone)) {
                return false;
            }
            out.insert(QStringLiteral("name"), name);
            out.insert(QStringLiteral("tone"), tone);
            return true;
        }
        if (kind == "progress") {
            return progressNode(json, path, out);
        }
        return fail("a status widget shows only row, text, icon and progress", member(path, "type"));
    }

    bool containerNode(const std::string& kind, const Json& json, const std::string& path, std::size_t depth,
                       QVariantMap& out)
    {
        if (kind == "group") {
            QString title;
            if (!readText(json, "title", path, kMostTitleLength, title)) {
                return false;
            }
            out.insert(QStringLiteral("title"), title);
        }
        QVariantList children;
        const Json* list = readArray(json, "children", path, m_mostNodes, false);
        if (!ok()) {
            return false;
        }
        if (list != nullptr) {
            children.reserve(static_cast<qsizetype>(list->size()));
            for (std::size_t index = 0; index < list->size(); ++index) {
                QVariant child = readNode((*list)[index], indexed(member(path, "children"), index), depth + 1);
                if (!ok()) {
                    return false;
                }
                children.append(std::move(child));
            }
        }
        out.insert(QStringLiteral("children"), children);
        return true;
    }

    bool textNode(const Json& json, const std::string& path, std::size_t most, QString& said, QVariantMap& out)
    {
        QString format;
        QString style;
        QString tone;
        if (!readText(json, "text", path, most, said) || !readChoice(json, "format", path, kFormats, format) ||
            !readChoice(json, "style", path, kTextStyles, style) || !readChoice(json, "tone", path, kTones, tone)) {
            return false;
        }
        out.insert(QStringLiteral("text"), said);
        out.insert(QStringLiteral("format"), format);
        out.insert(QStringLiteral("style"), style);
        out.insert(QStringLiteral("tone"), tone);
        return true;
    }

    bool textNode(const Json& json, const std::string& path, std::size_t most, QVariantMap& out)
    {
        QString said;
        return textNode(json, path, most, said, out);
    }

    bool buttonNode(const Json& json, const std::string& path, QVariantMap& out)
    {
        QString title;
        QString icon;
        QString style;
        bool enabled = true;
        if (!needsId(out, "button", path) || !readText(json, "title", path, kMostButtonTitleLength, title, true) ||
            !readText(json, "icon", path, kMostIconLength, icon) ||
            !readChoice(json, "style", path, kButtonStyles, style) || !readFlag(json, "enabled", path, enabled)) {
            return false;
        }
        out.insert(QStringLiteral("title"), title);
        out.insert(QStringLiteral("icon"), icon);
        out.insert(QStringLiteral("style"), style);
        out.insert(QStringLiteral("enabled"), enabled);
        return true;
    }

    bool fieldNode(const Json& json, const std::string& path, QVariantMap& out)
    {
        QString title;
        QString value;
        QString placeholder;
        bool enabled = true;
        if (!needsId(out, "field", path) || !readText(json, "title", path, kMostTitleLength, title) ||
            !readText(json, "value", path, kMostTextLength, value) ||
            !readText(json, "placeholder", path, kMostTitleLength, placeholder) ||
            !readFlag(json, "enabled", path, enabled)) {
            return false;
        }
        out.insert(QStringLiteral("title"), title);
        out.insert(QStringLiteral("value"), value);
        out.insert(QStringLiteral("placeholder"), placeholder);
        out.insert(QStringLiteral("enabled"), enabled);
        return true;
    }

    bool numberNode(const Json& json, const std::string& path, QVariantMap& out)
    {
        QString title;
        QString unit;
        bool enabled = true;
        std::optional<double> value;
        std::optional<double> low;
        std::optional<double> high;
        std::optional<double> step;
        if (!needsId(out, "number", path) || !readText(json, "title", path, kMostTitleLength, title) ||
            !readNumber(json, "value", path, value) || !readNumber(json, "min", path, low) ||
            !readNumber(json, "max", path, high) || !readNumber(json, "step", path, step) ||
            !readText(json, "unit", path, kMostUnitLength, unit) || !readFlag(json, "enabled", path, enabled)) {
            return false;
        }
        if (low && high && *low > *high) {
            return fail("min is above max", member(path, "min"));
        }
        if (step && *step <= 0.0) {
            return fail("step must be more than 0", member(path, "step"));
        }
        const double shown = value.value_or(low ? *low : high ? std::min(0.0, *high) : 0.0);
        if ((low && shown < *low) || (high && shown > *high)) {
            return fail("value is outside min and max", member(path, "value"));
        }
        out.insert(QStringLiteral("title"), title);
        out.insert(QStringLiteral("value"), shown);
        out.insert(QStringLiteral("unit"), unit);
        out.insert(QStringLiteral("enabled"), enabled);
        if (low) {
            out.insert(QStringLiteral("min"), *low);
        }
        if (high) {
            out.insert(QStringLiteral("max"), *high);
        }
        if (step) {
            out.insert(QStringLiteral("step"), *step);
        }
        return true;
    }

    bool toggleNode(const Json& json, const std::string& path, QVariantMap& out)
    {
        QString title;
        bool value = false;
        bool enabled = true;
        if (!needsId(out, "toggle", path) || !readText(json, "title", path, kMostTitleLength, title, true) ||
            !readFlag(json, "value", path, value) || !readFlag(json, "enabled", path, enabled)) {
            return false;
        }
        out.insert(QStringLiteral("title"), title);
        out.insert(QStringLiteral("value"), value);
        out.insert(QStringLiteral("enabled"), enabled);
        return true;
    }

    bool choiceNode(const Json& json, const std::string& path, QVariantMap& out)
    {
        QString title;
        QString value;
        bool enabled = true;
        if (!needsId(out, "choice", path) || !readText(json, "title", path, kMostTitleLength, title) ||
            !readText(json, "value", path, kMostIdLength, value) || !readFlag(json, "enabled", path, enabled)) {
            return false;
        }
        const Json* options = readArray(json, "options", path, kMostOptions, true);
        if (options == nullptr) {
            return false;
        }
        if (options->empty()) {
            return fail("options needs at least one option", member(path, "options"));
        }
        QVariantList list;
        bool chosen = value.isEmpty();
        for (std::size_t index = 0; index < options->size(); ++index) {
            const Json& option = (*options)[index];
            const std::string where = indexed(member(path, "options"), index);
            QString id;
            QString label;
            if (option.is_string()) {
                const std::string& written = option.get_ref<const std::string&>();
                if (written.empty() || characters(written) > kMostIdLength) {
                    return fail("an option written as text must be 1 to " + std::to_string(kMostIdLength) + " characters",
                                where);
                }
                id = toQString(written);
                label = id;
            } else if (option.is_object()) {
                if (!readText(option, "id", where, kMostIdLength, id, true) ||
                    !readText(option, "title", where, kMostTitleLength, label, true)) {
                    return false;
                }
            } else {
                return fail("an option must be text or an object with id and title", where);
            }
            chosen = chosen || id == value;
            list.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("title"), label}});
        }
        if (!chosen) {
            return fail("value is not the id of one of the options", member(path, "value"));
        }
        out.insert(QStringLiteral("title"), title);
        out.insert(QStringLiteral("value"), value);
        out.insert(QStringLiteral("options"), list);
        out.insert(QStringLiteral("enabled"), enabled);
        return true;
    }

    bool sliderNode(const Json& json, const std::string& path, QVariantMap& out)
    {
        QString title;
        bool enabled = true;
        std::optional<double> value;
        std::optional<double> low;
        std::optional<double> high;
        std::optional<double> step;
        if (!needsId(out, "slider", path) || !readText(json, "title", path, kMostTitleLength, title) ||
            !readNumber(json, "value", path, value) || !readNumber(json, "min", path, low) ||
            !readNumber(json, "max", path, high) || !readNumber(json, "step", path, step) ||
            !readFlag(json, "enabled", path, enabled)) {
            return false;
        }
        const double from = low.value_or(0.0);
        const double to = high.value_or(1.0);
        if (!(from < to)) {
            return fail("min must be below max", member(path, "min"));
        }
        if (step && *step <= 0.0) {
            return fail("step must be more than 0", member(path, "step"));
        }
        const double shown = value.value_or(from);
        if (shown < from || shown > to) {
            return fail("value is outside min and max", member(path, "value"));
        }
        out.insert(QStringLiteral("title"), title);
        out.insert(QStringLiteral("value"), shown);
        out.insert(QStringLiteral("min"), from);
        out.insert(QStringLiteral("max"), to);
        out.insert(QStringLiteral("step"), step.value_or(0.0));
        out.insert(QStringLiteral("enabled"), enabled);
        return true;
    }

    bool listNode(const Json& json, const std::string& path, QVariantMap& out)
    {
        QString selected;
        bool selectable = true;
        if (!needsId(out, "list", path) || !readText(json, "selected", path, kMostIdLength, selected) ||
            !readFlag(json, "selectable", path, selectable)) {
            return false;
        }
        const Json* items = readArray(json, "items", path, kMostListItems, true);
        if (items == nullptr) {
            return false;
        }
        QVariantList list;
        list.reserve(static_cast<qsizetype>(items->size()));
        for (std::size_t index = 0; index < items->size(); ++index) {
            const Json& item = (*items)[index];
            const std::string where = indexed(member(path, "items"), index);
            if (!item.is_object()) {
                return fail("a list item must be an object", where);
            }
            QString id;
            QString title;
            QString subtitle;
            QString icon;
            if (!readText(item, "id", where, kMostIdLength, id, true) ||
                !readText(item, "title", where, kMostTitleLength, title, true) ||
                !readText(item, "subtitle", where, kMostTitleLength, subtitle) ||
                !readText(item, "icon", where, kMostIconLength, icon)) {
                return false;
            }
            list.append(QVariantMap{{QStringLiteral("id"), id},
                                    {QStringLiteral("title"), title},
                                    {QStringLiteral("subtitle"), subtitle},
                                    {QStringLiteral("icon"), icon}});
        }
        out.insert(QStringLiteral("items"), list);
        out.insert(QStringLiteral("selected"), selected);
        out.insert(QStringLiteral("selectable"), selectable);
        return true;
    }

    bool tableNode(const Json& json, const std::string& path, QVariantMap& out)
    {
        const Json* columns = readArray(json, "columns", path, kMostColumns, true);
        if (columns == nullptr) {
            return false;
        }
        if (columns->empty()) {
            return fail("columns needs at least one column", member(path, "columns"));
        }
        QStringList headers;
        for (std::size_t index = 0; index < columns->size(); ++index) {
            const Json& column = (*columns)[index];
            if (!column.is_string() || characters(column.get_ref<const std::string&>()) > kMostColumnLength) {
                return fail("a column must be text of at most " + std::to_string(kMostColumnLength) + " characters",
                            indexed(member(path, "columns"), index));
            }
            headers.append(toQString(column.get_ref<const std::string&>()));
        }
        const Json* rows = readArray(json, "rows", path, kMostRows, false);
        if (!ok()) {
            return false;
        }
        QVariantList table;
        for (std::size_t row = 0; rows != nullptr && row < rows->size(); ++row) {
            const Json& cells = (*rows)[row];
            const std::string where = indexed(member(path, "rows"), row);
            if (!cells.is_array() || cells.size() > columns->size()) {
                return fail("a row must be a list of at most one cell a column", where);
            }
            QStringList line;
            for (std::size_t column = 0; column < columns->size(); ++column) {
                if (column >= cells.size() || cells[column].is_null()) {
                    line.append(QString());
                } else if (cells[column].is_string() &&
                           characters(cells[column].get_ref<const std::string&>()) <= kMostCellLength) {
                    line.append(toQString(cells[column].get_ref<const std::string&>()));
                } else if (cells[column].is_number()) {
                    line.append(numberText(cells[column].get<double>()));
                } else if (cells[column].is_boolean()) {
                    line.append(cells[column].get<bool>() ? QStringLiteral("true") : QStringLiteral("false"));
                } else {
                    return fail("a cell must be text of at most " + std::to_string(kMostCellLength) +
                                    " characters, a number, true, false or null",
                                indexed(where, column));
                }
            }
            table.append(QVariant(line));
        }
        out.insert(QStringLiteral("columns"), QVariant(headers));
        out.insert(QStringLiteral("rows"), table);
        return true;
    }

    bool chartNode(const Json& json, const std::string& path, QVariantMap& out)
    {
        QString title;
        QString kind;
        std::optional<double> height;
        std::optional<double> low;
        std::optional<double> high;
        if (!readText(json, "title", path, kMostTitleLength, title) ||
            !readChoice(json, "kind", path, kChartKinds, kind) || !readNumber(json, "height", path, height) ||
            !readNumber(json, "min", path, low) || !readNumber(json, "max", path, high)) {
            return false;
        }
        if (height && (*height < kLeastChartHeight || *height > kMostChartHeight)) {
            return fail("height must be from 60 to 480", member(path, "height"));
        }
        if (low && high && !(*low < *high)) {
            return fail("min must be below max", member(path, "min"));
        }
        const Json* series = readArray(json, "series", path, kMostSeries, true);
        if (series == nullptr) {
            return false;
        }
        QVariantList lines;
        for (std::size_t index = 0; index < series->size(); ++index) {
            const Json& line = (*series)[index];
            const std::string where = indexed(member(path, "series"), index);
            if (!line.is_object()) {
                return fail("a series must be an object", where);
            }
            QString label;
            QString color;
            if (!readText(line, "title", where, kMostTitleLength, label) || !readColor(line, "color", where, color)) {
                return false;
            }
            const Json* points = readArray(line, "points", where, kMostPoints, true);
            if (points == nullptr) {
                return false;
            }
            QVariantList values;
            values.reserve(static_cast<qsizetype>(points->size()));
            for (std::size_t point = 0; point < points->size(); ++point) {
                const Json& number = (*points)[point];
                if (!number.is_number() || !std::isfinite(number.get<double>())) {
                    return fail("a point must be a number", indexed(member(where, "points"), point));
                }
                values.append(number.get<double>());
            }
            lines.append(QVariantMap{
                {QStringLiteral("title"), label}, {QStringLiteral("color"), color}, {QStringLiteral("points"), values}});
        }
        out.insert(QStringLiteral("title"), title);
        out.insert(QStringLiteral("kind"), kind);
        out.insert(QStringLiteral("series"), lines);
        out.insert(QStringLiteral("height"), height.value_or(0.0));
        if (low) {
            out.insert(QStringLiteral("min"), *low);
        }
        if (high) {
            out.insert(QStringLiteral("max"), *high);
        }
        return true;
    }

    bool sideWithin(double value, const char* field, const std::string& path)
    {
        return (value >= 1.0 && value <= kMostSide) || fail(std::string(field) + " must be from 1 to 2000", member(path, field));
    }

    bool strokeOf(const Json& shape, const std::string& where, QVariantMap& out)
    {
        QString stroke;
        std::optional<double> width;
        if (!readColor(shape, "stroke", where, stroke) || !readNumber(shape, "strokeWidth", where, width)) {
            return false;
        }
        if (width && (*width < 0.0 || *width > kMostStrokeWidth)) {
            return fail("strokeWidth must be from 0 to 20", member(where, "strokeWidth"));
        }
        out.insert(QStringLiteral("stroke"), stroke);
        out.insert(QStringLiteral("strokeWidth"), width.value_or(1.0));
        return true;
    }

    bool shapeOf(const Json& shape, const std::string& where, QVariantMap& out)
    {
        QString kind;
        if (!shape.is_object()) {
            return fail("a shape must be an object", where);
        }
        if (!shape.contains("kind")) {
            return fail("a shape needs a kind", where);
        }
        if (!readChoice(shape, "kind", where, kShapeKinds, kind)) {
            return false;
        }
        out.insert(QStringLiteral("kind"), kind);
        QString fill;
        if (!readColor(shape, "fill", where, fill)) {
            return false;
        }
        out.insert(QStringLiteral("fill"), fill);
        const auto numbers = [&](std::initializer_list<const char*> fields) {
            for (const char* field : fields) {
                double value = 0.0;
                if (!readNeeded(shape, field, where, value)) {
                    return false;
                }
                out.insert(QString::fromLatin1(field), value);
            }
            return true;
        };
        if (kind == u"rect") {
            std::optional<double> radius;
            if (!numbers({"x", "y", "width", "height"}) || !readNumber(shape, "radius", where, radius) ||
                !strokeOf(shape, where, out)) {
                return false;
            }
            if (out.value(QStringLiteral("width")).toDouble() < 0.0 || out.value(QStringLiteral("height")).toDouble() < 0.0) {
                return fail("width and height must not be below 0", where);
            }
            out.insert(QStringLiteral("radius"), std::max(0.0, radius.value_or(0.0)));
            return true;
        }
        if (kind == u"circle") {
            if (!numbers({"x", "y", "radius"}) || !strokeOf(shape, where, out)) {
                return false;
            }
            return out.value(QStringLiteral("radius")).toDouble() >= 0.0 || fail("radius must not be below 0", where);
        }
        if (kind == u"line") {
            return numbers({"x1", "y1", "x2", "y2"}) && strokeOf(shape, where, out);
        }
        if (kind == u"polyline") {
            bool closed = false;
            const Json* points = readArray(shape, "points", where, kMostPolylineNumbers, true);
            if (points == nullptr || !readFlag(shape, "closed", where, closed) || !strokeOf(shape, where, out)) {
                return false;
            }
            if (points->size() < 4 || points->size() % 2 != 0) {
                return fail("points must hold x and y of at least two points", member(where, "points"));
            }
            QVariantList values;
            for (std::size_t index = 0; index < points->size(); ++index) {
                const Json& number = (*points)[index];
                if (!number.is_number() || !std::isfinite(number.get<double>())) {
                    return fail("a point must be a number", indexed(member(where, "points"), index));
                }
                values.append(number.get<double>());
            }
            out.insert(QStringLiteral("points"), values);
            out.insert(QStringLiteral("closed"), closed);
            return true;
        }
        QString said;
        QString color;
        QString size;
        if (!numbers({"x", "y"}) || !readText(shape, "text", where, kMostTitleLength, said, true) ||
            !readColor(shape, "color", where, color) || !readChoice(shape, "size", where, kShapeSizes, size)) {
            return false;
        }
        out.insert(QStringLiteral("text"), said);
        out.insert(QStringLiteral("color"), color);
        out.insert(QStringLiteral("size"), size);
        return true;
    }

    bool canvasNode(const Json& json, const std::string& path, QVariantMap& out)
    {
        QString title;
        double width = 0.0;
        double height = 0.0;
        if (!readText(json, "title", path, kMostTitleLength, title) || !readNeeded(json, "width", path, width) ||
            !readNeeded(json, "height", path, height) || !sideWithin(width, "width", path) ||
            !sideWithin(height, "height", path)) {
            return false;
        }
        const Json* shapes = readArray(json, "shapes", path, kMostShapes, false);
        if (!ok()) {
            return false;
        }
        QVariantList drawn;
        for (std::size_t index = 0; shapes != nullptr && index < shapes->size(); ++index) {
            QVariantMap shape;
            if (!shapeOf((*shapes)[index], indexed(member(path, "shapes"), index), shape)) {
                return false;
            }
            drawn.append(shape);
        }
        out.insert(QStringLiteral("title"), title);
        out.insert(QStringLiteral("width"), width);
        out.insert(QStringLiteral("height"), height);
        out.insert(QStringLiteral("shapes"), drawn);
        return true;
    }

    bool imageNode(const Json& json, const std::string& path, QVariantMap& out)
    {
        QString given;
        QString title;
        std::optional<double> width;
        std::optional<double> height;
        if (!readText(json, "path", path, 260, given, true) || !readText(json, "title", path, kMostTitleLength, title) ||
            !readNumber(json, "width", path, width) || !readNumber(json, "height", path, height) ||
            (width && !sideWithin(*width, "width", path)) || (height && !sideWithin(*height, "height", path))) {
            return false;
        }
        const std::string where = member(path, "path");
        const std::string relative = given.toStdString();
        if (m_folder.empty()) {
            return fail("pictures come from the extension's folder, and this logic has none", where);
        }
        if (!ext::safeArchivePath(relative)) {
            return fail("the picture must be a file inside the extension's folder", where);
        }
        std::string lower = relative;
        std::ranges::transform(lower, lower.begin(),
                               [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
        if (!lower.ends_with(".png") && !lower.ends_with(".svg")) {
            return fail("the picture must be a .png or .svg file", where);
        }
        std::error_code error;
        const fs::path root = fs::weakly_canonical(m_folder, error);
        if (error) {
            return fail("the extension's folder cannot be read", where);
        }
        const fs::path file = fs::weakly_canonical(
            m_folder / fs::path(std::u8string(reinterpret_cast<const char8_t*>(relative.data()), relative.size())), error);
        if (error || !inside(root, file)) {
            return fail("the picture must be a file inside the extension's folder", where);
        }
        if (!fs::is_regular_file(file, error) || error) {
            return fail("there is no picture at this path in the extension's folder", where);
        }
        const std::uintmax_t bytes = fs::file_size(file, error);
        if (error || bytes > kMostUiImageBytes) {
            return fail("the picture is larger than 2 MiB", where);
        }
        out.insert(QStringLiteral("source"), QUrl::fromLocalFile(QString::fromStdWString(file.wstring())).toString());
        out.insert(QStringLiteral("title"), title);
        out.insert(QStringLiteral("width"), width.value_or(0.0));
        out.insert(QStringLiteral("height"), height.value_or(0.0));
        return true;
    }

    bool progressNode(const Json& json, const std::string& path, QVariantMap& out)
    {
        QString title;
        std::optional<double> value;
        if (!readText(json, "title", path, kMostTitleLength, title) || !readNumber(json, "value", path, value)) {
            return false;
        }
        if (value && (*value < 0.0 || *value > 1.0)) {
            return fail("value must be from 0 to 1, or null while it is not known", member(path, "value"));
        }
        out.insert(QStringLiteral("title"), title);
        out.insert(QStringLiteral("value"), value.value_or(-1.0));
        return true;
    }

    fs::path m_folder;
    std::size_t m_mostNodes = kMostUiNodes;
    bool m_statusOnly = false;
    std::size_t m_nodes = 0;
    std::string m_problem;
};

UiCheck checkTree(std::string_view json, const fs::path& folder, std::size_t mostNodes, bool statusOnly)
{
    UiCheck check;
    if (json.size() > kMostUiTreeBytes) {
        check.problem = "the tree is larger than 256 KiB as JSON";
        return check;
    }
    if (!nestingWithin(json, kMostJsonNesting)) {
        check.problem = "the tree is nested deeper than " + std::to_string(kMostUiDepth) + " levels";
        return check;
    }
    const Json tree = Json::parse(json.begin(), json.end(), nullptr, false);
    if (tree.is_discarded()) {
        check.problem = "the tree is not valid JSON";
        return check;
    }
    Reader reader(folder, mostNodes, statusOnly);
    QVariant value = reader.readNode(tree, "$", 1);
    if (!reader.ok()) {
        check.problem = reader.problem();
        return check;
    }
    check.value = std::move(value);
    return check;
}

std::string dumped(const Json& json)
{
    return json.dump(-1, ' ', false, Json::error_handler_t::replace);
}

}

UiCheck checkUiTree(std::string_view json, const std::filesystem::path& folder)
{
    return checkTree(json, folder, kMostUiNodes, false);
}

UiCheck checkStatusTree(std::string_view json)
{
    return checkTree(json, {}, kMostStatusNodes, true);
}

UiCheck checkMenuItems(std::string_view json)
{
    UiCheck check;
    if (json.size() > kMostUiTreeBytes || !nestingWithin(json, kMostJsonNesting)) {
        check.problem = "the menu items are too large";
        return check;
    }
    const Json items = Json::parse(json.begin(), json.end(), nullptr, false);
    if (items.is_discarded() || !items.is_array()) {
        check.problem = "the menu items must be a list";
        return check;
    }
    if (items.size() > kMostMenuItems) {
        check.problem = "the menu has more than " + std::to_string(kMostMenuItems) + " items";
        return check;
    }
    Reader reader({}, 0, false);
    QVariantList list;
    for (std::size_t index = 0; index < items.size(); ++index) {
        const Json& item = items[index];
        const std::string path = indexed("$", index);
        if (!item.is_object()) {
            check.problem = "a menu item must be an object at " + path;
            return check;
        }
        QString id;
        QString title;
        QString icon;
        bool enabled = true;
        bool checked = false;
        if (!reader.readText(item, "id", path, kMostIdLength, id, true) ||
            !reader.readText(item, "title", path, kMostMenuTitleLength, title, true) ||
            !reader.readText(item, "icon", path, kMostIconLength, icon) || !reader.readFlag(item, "enabled", path, enabled) ||
            !reader.readFlag(item, "checked", path, checked)) {
            check.problem = reader.problem();
            return check;
        }
        const auto mark = item.find("checked");
        list.append(QVariantMap{{QStringLiteral("id"), id},
                                {QStringLiteral("title"), title},
                                {QStringLiteral("icon"), icon},
                                {QStringLiteral("enabled"), enabled},
                                {QStringLiteral("checkable"), mark != item.end() && mark->is_boolean()},
                                {QStringLiteral("checked"), checked}});
    }
    check.value = list;
    return check;
}

std::vector<PanelDeclaration> panelDeclarations(const ExtensionHost& host, const QString& language)
{
    std::vector<PanelDeclaration> declarations;
    for (const ExtensionHost::Extension& extension : host.extensions()) {
        if (!extension.active || !extension.manifest) {
            continue;
        }
        const ext::Manifest& manifest = *extension.manifest;
        const QString extensionName = extensionText(manifest, "name", manifest.name, language);
        for (const ext::PanelContribution& panel : manifest.contributes.panels) {
            declarations.push_back(
                {.extension = manifest.id,
                 .name = panel.name,
                 .where = panel.where,
                 .script = panel.script,
                 .title = extensionText(manifest, "panels." + panel.name + ".title", panel.title, language),
                 .icon = toQString(panel.icon),
                 .extensionName = extensionName,
                 .folder = extension.directory});
        }
    }
    return declarations;
}

ExtensionPanels::ExtensionPanels(QObject* parent)
    : QObject(parent)
{
    s_instance = this;
    m_flush.setTimerType(Qt::PreciseTimer);
    m_flush.setSingleShot(true); // source-rule-allow R9: renders beyond 30 a second wait for the panel's next turn
    connect(&m_flush, &QTimer::timeout, this, &ExtensionPanels::flush);
    m_teller = [](std::uint64_t logic, const QString& kind, const QString& id, const QString& json) {
        return LogicServices::instance().tell(logic, kind, id, json);
    };
    m_inbox->owner = this;
    const auto guarded = [inbox = m_inbox](auto handle) {
        return [inbox, handle](const LogicCaller& caller, std::string_view json) {
            try {
                return handle(*inbox, caller, json);
            } catch (...) {
                return refused(kTreeCode, "what was sent could not be read");
            }
        };
    };
    LogicServices& services = LogicServices::instance();
    services.offerCall(std::string(kServices[0]), guarded(&ExtensionPanels::render));
    services.offerCall(std::string(kServices[1]), guarded(&ExtensionPanels::status));
    services.offerCall(std::string(kServices[2]), guarded(&ExtensionPanels::menu));
    services.offerCall(std::string(kServices[3]), guarded(&ExtensionPanels::close));
    connect(&services, &LogicServices::logicStarted, this, &ExtensionPanels::rebuild);
    connect(&services, &LogicServices::logicEnded, this, [this](quint64 logic) { forget(logic); });
}

ExtensionPanels::~ExtensionPanels()
{
    for (const std::string_view service : kServices) {
        LogicServices::instance().withdraw(service);
    }
    {
        const std::scoped_lock lock(m_inbox->mutex);
        m_inbox->owner = nullptr;
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

ExtensionPanels* ExtensionPanels::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT(s_instance != nullptr);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void ExtensionPanels::watch(ExtensionHost& host)
{
    const auto refresh = [this, &host] {
        const AppSettings* settings = AppSettings::instance();
        setDeclarations(panelDeclarations(host, settings != nullptr ? settings->interfaceLanguage() : QString()));
    };
    connect(&host, &ExtensionHost::extensionsChanged, this, refresh);
    if (const AppSettings* settings = AppSettings::instance()) {
        connect(settings, &AppSettings::languageChanged, this, refresh);
    }
    refresh();
}

void ExtensionPanels::setDeclarations(std::vector<PanelDeclaration> declarations)
{
    if (declarations == m_declarations) {
        return;
    }
    {
        const std::scoped_lock lock(m_inbox->mutex);
        m_inbox->declarations = declarations;
    }
    m_declarations = std::move(declarations);
    rebuild();
}

void ExtensionPanels::setTeller(Teller teller)
{
    m_teller = std::move(teller);
}

QVariant ExtensionPanels::tree(const QString& key) const
{
    const std::optional<Slot> slot = slotOf(key);
    if (!slot) {
        return {};
    }
    const auto found = m_shown.find(*slot);
    return found != m_shown.end() ? found->second.value : QVariant();
}

bool ExtensionPanels::send(quint64 logic, const QString& panel, const QString& type, const QString& id,
                           const QVariant& value) const
{
    if (!oneOf(kEventTypes, type.toStdString()) || panel.isEmpty() || !m_teller) {
        return false;
    }
    const QJsonObject event{{QStringLiteral("type"), type},
                            {QStringLiteral("id"), id},
                            {QStringLiteral("value"), QJsonValue::fromVariant(value)}};
    return m_teller(logic, QStringLiteral("ui"), panel,
                    QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact)));
}

QString ExtensionPanels::iconOf(const QString& name) const
{
    if (name.isEmpty()) {
        return {};
    }
    readIcons();
    return m_icons.isEmpty() || m_icons.contains(name) ? name : QStringLiteral("puzzle");
}

void ExtensionPanels::readIcons() const
{
    if (std::exchange(m_iconsRead, true)) {
        return;
    }
    QFile icons(QStringLiteral(":/qt/qml/GameScrcpy/Icons/IconPaths.js"));
    if (!icons.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    static const QRegularExpression kIconLine(QStringLiteral("^\\s*\"([a-z0-9-]+)\":"));
    while (!icons.atEnd()) {
        const QRegularExpressionMatch found = kIconLine.match(QString::fromUtf8(icons.readLine()));
        if (found.hasMatch()) {
            m_icons.insert(found.captured(1));
        }
    }
}

QVariantMap ExtensionPanels::sideState(const QString& serial) const
{
    const auto found = m_sides.find(serial);
    const Side side = found != m_sides.end() ? found->second : Side{};
    return {{QStringLiteral("panel"), side.panel}, {QStringLiteral("open"), side.open}, {QStringLiteral("width"), side.width}};
}

void ExtensionPanels::setSideState(const QString& serial, const QString& panel, bool open, int width)
{
    m_sides.insert_or_assign(serial, Side{.panel = panel, .open = open, .width = width});
}

script::ServiceAnswer ExtensionPanels::render(Inbox& inbox, const LogicCaller& caller, std::string_view json)
{
    if (!nestingWithin(json, kMostJsonNesting)) {
        return refused(kTreeCode, "the tree is nested deeper than " + std::to_string(kMostUiDepth) + " levels");
    }
    const Json message = Json::parse(json.begin(), json.end(), nullptr, false);
    const auto panel = message.is_object() ? message.find("panel") : message.end();
    const auto given = message.is_object() ? message.find("tree") : message.end();
    if (!message.is_object() || panel == message.end() || !panel->is_string() || given == message.end()) {
        return refused(kTreeCode, "a panel is drawn with a name and a tree");
    }
    std::string problem;
    const std::optional<PanelDeclaration> declaration =
        declarationFor(inbox, caller, Kind::Panel, panel->get_ref<const std::string&>(), problem);
    if (!declaration) {
        return refused(kPanelCode, problem);
    }
    QVariant value = QVariantMap{{QStringLiteral("type"), QStringLiteral("column")},
                                 {QStringLiteral("id"), QString()},
                                 {QStringLiteral("children"), QVariantList()}};
    if (!given->is_null()) {
        UiCheck check = checkUiTree(dumped(*given), declaration->folder);
        if (!check.ok()) {
            return refused(kTreeCode, check.problem);
        }
        value = std::move(check.value);
    }
    post(inbox, Slot{.logic = caller.logic, .kind = Kind::Panel, .name = declaration->name}, std::move(value));
    return {};
}

script::ServiceAnswer ExtensionPanels::status(Inbox& inbox, const LogicCaller& caller, std::string_view json)
{
    if (!nestingWithin(json, kMostJsonNesting)) {
        return refused(kTreeCode, "the status widget is nested too deeply");
    }
    const Json message = Json::parse(json.begin(), json.end(), nullptr, false);
    const auto name = message.is_object() ? message.find("name") : message.end();
    const auto given = message.is_object() ? message.find("tree") : message.end();
    if (!message.is_object() || name == message.end() || !name->is_string() || given == message.end()) {
        return refused(kTreeCode, "a status widget is drawn with a name and a tree");
    }
    std::string problem;
    const std::optional<PanelDeclaration> declaration =
        declarationFor(inbox, caller, Kind::Status, name->get_ref<const std::string&>(), problem);
    if (!declaration) {
        return refused(kPanelCode, problem);
    }
    QVariant value;
    if (!given->is_null()) {
        UiCheck check = checkStatusTree(dumped(*given));
        if (!check.ok()) {
            return refused(kTreeCode, check.problem);
        }
        value = std::move(check.value);
    }
    post(inbox, Slot{.logic = caller.logic, .kind = Kind::Status, .name = declaration->name}, std::move(value));
    return {};
}

script::ServiceAnswer ExtensionPanels::menu(Inbox& inbox, const LogicCaller& caller, std::string_view json)
{
    if (!nestingWithin(json, kMostJsonNesting)) {
        return refused(kTreeCode, "the menu items are nested too deeply");
    }
    const Json message = Json::parse(json.begin(), json.end(), nullptr, false);
    const auto items = message.is_object() ? message.find("items") : message.end();
    if (!message.is_object() || items == message.end()) {
        return refused(kTreeCode, "the menu is filled with a list of items");
    }
    std::string problem;
    const std::optional<PanelDeclaration> declaration = declarationFor(inbox, caller, Kind::Menu, {}, problem);
    if (!declaration) {
        return refused(kPanelCode, problem);
    }
    QVariant value;
    if (!items->is_null()) {
        UiCheck check = checkMenuItems(dumped(*items));
        if (!check.ok()) {
            return refused(kTreeCode, check.problem);
        }
        if (!check.value.toList().isEmpty()) {
            value = std::move(check.value);
        }
    }
    post(inbox, Slot{.logic = caller.logic, .kind = Kind::Menu, .name = declaration->name}, std::move(value));
    return {};
}

script::ServiceAnswer ExtensionPanels::close(Inbox& inbox, const LogicCaller& caller, std::string_view json)
{
    const Json message = Json::parse(json.begin(), json.end(), nullptr, false);
    const auto panel = message.is_object() ? message.find("panel") : message.end();
    if (!message.is_object() || panel == message.end() || !panel->is_string()) {
        return refused(kTreeCode, "a panel is closed by its name");
    }
    std::string problem;
    const std::optional<PanelDeclaration> declaration =
        declarationFor(inbox, caller, Kind::Panel, panel->get_ref<const std::string&>(), problem);
    if (!declaration) {
        return refused(kPanelCode, problem);
    }
    const QString key = keyOf(Slot{.logic = caller.logic, .kind = Kind::Panel, .name = declaration->name});
    const std::scoped_lock lock(inbox.mutex);
    if (ExtensionPanels* owner = inbox.owner) {
        QMetaObject::invokeMethod(owner, [owner, key] { emit owner->closeRequested(key); }, Qt::QueuedConnection);
    }
    return {};
}

std::optional<PanelDeclaration> ExtensionPanels::declarationFor(Inbox& inbox, const LogicCaller& caller, Kind kind,
                                                                std::string_view name, std::string& problem)
{
    if (caller.extension.isEmpty()) {
        problem = "only the logic of an extension has panels; the extension declares them in contributes.panels";
        return std::nullopt;
    }
    const std::string extension = caller.extension.toStdString();
    const std::string script = caller.script.toStdString();
    std::optional<PanelDeclaration> found;
    {
        const std::scoped_lock lock(inbox.mutex);
        for (const PanelDeclaration& declaration : inbox.declarations) {
            const bool inMenu = declaration.where == ext::PanelPlace::Menu;
            const bool wanted = kind == Kind::Menu ? inMenu && declaration.script == script : declaration.name == name;
            if (declaration.extension == extension && wanted) {
                found = declaration;
                break;
            }
        }
    }
    if (!found) {
        problem = kind == Kind::Menu ? extension + " declares no right-click menu for its script " + script
                                     : extension + " declares no panel named " + shortened(name);
        return std::nullopt;
    }
    if (found->script != script) {
        problem = "the panel " + found->name + " is drawn by the script " + found->script + ", not by " + script;
        return std::nullopt;
    }
    switch (found->where) {
    case ext::PanelPlace::Mirror:
    case ext::PanelPlace::Page:
        if (kind != Kind::Panel) {
            problem = found->name + " is a panel; draw it with ui.panel(\"" + found->name + "\").render()";
            return std::nullopt;
        }
        break;
    case ext::PanelPlace::Status:
        if (kind != Kind::Status) {
            problem = found->name + " is a status widget; draw it with ui.status(\"" + found->name + "\").render()";
            return std::nullopt;
        }
        break;
    case ext::PanelPlace::Menu:
        if (kind != Kind::Menu) {
            problem = found->name + " is the right-click menu; fill it with ui.menu(items)";
            return std::nullopt;
        }
        break;
    }
    const bool phone = !caller.device.isEmpty();
    const bool page = found->where == ext::PanelPlace::Page;
    if (page == phone) {
        problem = page ? "the page " + found->name + " belongs to the main window, and this logic runs for a phone"
                       : found->name + " belongs to a phone's mirror window, and this logic runs without a phone";
        return std::nullopt;
    }
    return found;
}

void ExtensionPanels::post(Inbox& inbox, Slot slot, QVariant value)
{
    const std::scoped_lock lock(inbox.mutex);
    ExtensionPanels* owner = inbox.owner;
    if (owner == nullptr || !inbox.pending.insert_or_assign(slot, std::move(value)).second) {
        return;
    }
    QMetaObject::invokeMethod(owner, [owner, slot = std::move(slot)] { owner->collect(slot); }, Qt::QueuedConnection);
}

void ExtensionPanels::collect(const Slot& slot)
{
    QVariant value;
    {
        const std::scoped_lock lock(m_inbox->mutex);
        const auto found = m_inbox->pending.find(slot);
        if (found == m_inbox->pending.end()) {
            return;
        }
        value = std::move(found->second);
        m_inbox->pending.erase(found);
    }
    deliver(slot, std::move(value));
}

void ExtensionPanels::deliver(const Slot& slot, QVariant value)
{
    const std::vector<LogicCaller> live = LogicServices::instance().running();
    if (std::ranges::none_of(live, [&slot](const LogicCaller& caller) { return caller.logic == slot.logic; })) {
        return;
    }
    Shown& shown = m_shown[slot];
    if (!shown.waiting && std::chrono::steady_clock::now() - shown.at >= kUiRenderGap) {
        show(slot, std::move(value));
        return;
    }
    shown.next = std::move(value);
    shown.waiting = true;
    flush();
}

void ExtensionPanels::show(const Slot& slot, QVariant value)
{
    Shown& shown = m_shown[slot];
    const bool had = shown.value.isValid();
    const bool has = value.isValid();
    shown.value = std::move(value);
    shown.next = {};
    shown.waiting = false;
    shown.at = std::chrono::steady_clock::now();
    if (slot.kind == Kind::Panel) {
        emit treeChanged(keyOf(slot));
        return;
    }
    if (!has) {
        m_shown.erase(slot);
    }
    if (had != has || slot.kind == Kind::Menu) {
        rebuild();
    }
    if (slot.kind == Kind::Status && has) {
        emit treeChanged(keyOf(slot));
    }
}

void ExtensionPanels::flush()
{
    const auto now = std::chrono::steady_clock::now();
    std::vector<Slot> due;
    std::optional<std::chrono::steady_clock::time_point> next;
    for (const auto& [slot, shown] : m_shown) {
        if (!shown.waiting) {
            continue;
        }
        const auto at = shown.at + kUiRenderGap;
        if (at <= now) {
            due.push_back(slot);
        } else {
            next = next ? std::min(*next, at) : at;
        }
    }
    for (const Slot& slot : due) {
        const auto found = m_shown.find(slot);
        if (found != m_shown.end() && found->second.waiting) {
            show(slot, std::move(found->second.next));
        }
    }
    if (!next) {
        m_flush.stop();
        return;
    }
    const auto wait = std::chrono::ceil<std::chrono::milliseconds>(*next - std::chrono::steady_clock::now());
    m_flush.start(std::max(wait, std::chrono::milliseconds(1)));
}

void ExtensionPanels::forget(std::uint64_t logic)
{
    {
        const std::scoped_lock lock(m_inbox->mutex);
        std::erase_if(m_inbox->pending, [logic](const auto& item) { return item.first.logic == logic; });
    }
    QStringList gone;
    for (auto it = m_shown.begin(); it != m_shown.end();) {
        if (it->first.logic != logic) {
            ++it;
            continue;
        }
        if (it->first.kind == Kind::Panel) {
            gone.append(keyOf(it->first));
        }
        it = m_shown.erase(it);
    }
    rebuild();
    for (const QString& key : gone) {
        emit treeChanged(key);
    }
}

void ExtensionPanels::rebuild()
{
    QVariantList shownPanels;
    QVariantList shownMenus;
    for (const LogicCaller& caller : LogicServices::instance().running()) {
        if (caller.extension.isEmpty()) {
            continue;
        }
        const std::string extension = caller.extension.toStdString();
        const std::string script = caller.script.toStdString();
        const bool phone = !caller.device.isEmpty();
        for (const PanelDeclaration& declaration : m_declarations) {
            if (declaration.extension != extension || declaration.script != script ||
                (declaration.where == ext::PanelPlace::Page) == phone) {
                continue;
            }
            const Kind kind = declaration.where == ext::PanelPlace::Status ? Kind::Status
                              : declaration.where == ext::PanelPlace::Menu ? Kind::Menu
                                                                           : Kind::Panel;
            const Slot slot{.logic = caller.logic, .kind = kind, .name = declaration.name};
            const auto shown = m_shown.find(slot);
            const bool drawn = shown != m_shown.end() && shown->second.value.isValid();
            if (kind != Kind::Panel && !drawn) {
                continue;
            }
            QVariantMap entry{
                {QStringLiteral("key"), keyOf(slot)},
                {QStringLiteral("id"), toQString(declaration.extension + '/' + declaration.name)},
                {QStringLiteral("logic"), QVariant::fromValue(static_cast<qulonglong>(caller.logic))},
                {QStringLiteral("extension"), toQString(declaration.extension)},
                {QStringLiteral("extensionName"), declaration.extensionName},
                {QStringLiteral("name"), toQString(declaration.name)},
                {QStringLiteral("title"), declaration.title},
                {QStringLiteral("icon"), iconOf(declaration.icon)},
                {QStringLiteral("where"), toQString(ext::kPanelPlaceNames[static_cast<std::size_t>(declaration.where)])},
                {QStringLiteral("device"), caller.device}};
            if (kind == Kind::Menu) {
                entry.insert(QStringLiteral("items"), shown->second.value);
                shownMenus.append(entry);
            } else {
                shownPanels.append(entry);
            }
        }
    }
    if (shownPanels != m_panels) {
        m_panels = std::move(shownPanels);
        emit panelsChanged();
    }
    if (shownMenus != m_menus) {
        m_menus = std::move(shownMenus);
        emit menusChanged();
    }
}

QString ExtensionPanels::keyOf(const Slot& slot)
{
    const QChar kind = slot.kind == Kind::Panel ? QChar(u'p') : slot.kind == Kind::Status ? QChar(u's') : QChar(u'm');
    return QString(kind) + u'/' + QString::number(slot.logic) + u'/' + toQString(slot.name);
}

std::optional<ExtensionPanels::Slot> ExtensionPanels::slotOf(const QString& key)
{
    const QStringList parts = key.split(u'/');
    if (parts.size() != 3 || parts[0].size() != 1) {
        return std::nullopt;
    }
    bool number = false;
    const qulonglong logic = parts[1].toULongLong(&number);
    if (!number) {
        return std::nullopt;
    }
    Kind kind = Kind::Panel;
    switch (parts[0].front().unicode()) {
    case u'p':
        kind = Kind::Panel;
        break;
    case u's':
        kind = Kind::Status;
        break;
    case u'm':
        kind = Kind::Menu;
        break;
    default:
        return std::nullopt;
    }
    return Slot{.logic = logic, .kind = kind, .name = parts[2].toStdString()};
}

}
