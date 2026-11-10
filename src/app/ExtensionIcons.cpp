#include "ExtensionIcons.h"

#include <QFile>
#include <QStringList>
#include <QXmlStreamReader>

#include <algorithm>
#include <map>
#include <mutex>
#include <optional>
#include <system_error>

namespace gs::app {

namespace {

using namespace Qt::StringLiterals;

QString number(double value)
{
    return QString::number(value, 'g', 6);
}

std::optional<double> numberOf(const QXmlStreamAttributes& attributes, QStringView name, std::optional<double> fallback)
{
    if (!attributes.hasAttribute(name)) {
        return fallback;
    }
    bool ok = false;
    const double value = attributes.value(name).toDouble(&ok);
    return ok ? std::optional<double>(value) : std::nullopt;
}

bool pathDataSafe(QStringView data)
{
    constexpr QStringView kCommands = u"MmLlHhVvCcSsQqTtAaZz.,-+eE";
    return !data.trimmed().isEmpty() &&
           std::ranges::all_of(data, [&](QChar c) { return c.isDigit() || c.isSpace() || kCommands.contains(c); });
}

std::optional<QString> pointsPath(QStringView points, bool closed)
{
    QString cleaned = points.toString();
    cleaned.replace(u',', u' ');
    const QStringList numbers = cleaned.simplified().split(u' ', Qt::SkipEmptyParts);
    if (numbers.size() < 4 || numbers.size() % 2 != 0) {
        return std::nullopt;
    }
    QStringList tokens;
    for (qsizetype index = 0; index < numbers.size(); index += 2) {
        bool okX = false;
        bool okY = false;
        const double x = numbers[index].toDouble(&okX);
        const double y = numbers[index + 1].toDouble(&okY);
        if (!okX || !okY) {
            return std::nullopt;
        }
        tokens << (index == 0 ? u"M"_s : u"L"_s) << number(x) << number(y);
    }
    if (closed) {
        tokens << u"Z"_s;
    }
    return tokens.join(u' ');
}

std::optional<QString> shapePath(QStringView name, const QXmlStreamAttributes& a)
{
    if (name == u"path") {
        const QStringView data = a.value(u"d");
        return pathDataSafe(data) ? std::optional<QString>(data.toString()) : std::nullopt;
    }
    if (name == u"circle") {
        const auto cx = numberOf(a, u"cx", 0.0);
        const auto cy = numberOf(a, u"cy", 0.0);
        const auto r = numberOf(a, u"r", std::nullopt);
        if (!cx || !cy || !r || *r <= 0) {
            return std::nullopt;
        }
        const QString radius = number(*r);
        return QStringList{u"M"_s, number(*cx - *r), number(*cy), u"A"_s, radius, radius, u"0 1 0"_s,
                           number(*cx + *r), number(*cy), u"A"_s, radius, radius, u"0 1 0"_s, number(*cx - *r),
                           number(*cy), u"Z"_s}
            .join(u' ');
    }
    if (name == u"rect") {
        const auto x = numberOf(a, u"x", 0.0);
        const auto y = numberOf(a, u"y", 0.0);
        const auto w = numberOf(a, u"width", std::nullopt);
        const auto h = numberOf(a, u"height", std::nullopt);
        const auto rx = numberOf(a, u"rx", std::nullopt);
        const auto ry = numberOf(a, u"ry", std::nullopt);
        if (!x || !y || !w || !h || *w <= 0 || *h <= 0) {
            return std::nullopt;
        }
        const double radiusX = std::clamp(rx.value_or(ry.value_or(0.0)), 0.0, *w / 2);
        const double radiusY = std::clamp(ry.value_or(rx.value_or(0.0)), 0.0, *h / 2);
        if (radiusX <= 0 || radiusY <= 0) {
            return QStringList{u"M"_s, number(*x), number(*y), u"H"_s, number(*x + *w), u"V"_s, number(*y + *h),
                               u"H"_s, number(*x), u"Z"_s}
                .join(u' ');
        }
        const QStringList arc{u"A"_s, number(radiusX), number(radiusY), u"0 0 1"_s};
        QStringList tokens{u"M"_s, number(*x + radiusX), number(*y), u"H"_s, number(*x + *w - radiusX)};
        tokens << arc << number(*x + *w) << number(*y + radiusY) << u"V"_s << number(*y + *h - radiusY);
        tokens << arc << number(*x + *w - radiusX) << number(*y + *h) << u"H"_s << number(*x + radiusX);
        tokens << arc << number(*x) << number(*y + *h - radiusY) << u"V"_s << number(*y + radiusY);
        tokens << arc << number(*x + radiusX) << number(*y) << u"Z"_s;
        return tokens.join(u' ');
    }
    if (name == u"line") {
        const auto x1 = numberOf(a, u"x1", 0.0);
        const auto y1 = numberOf(a, u"y1", 0.0);
        const auto x2 = numberOf(a, u"x2", 0.0);
        const auto y2 = numberOf(a, u"y2", 0.0);
        if (!x1 || !y1 || !x2 || !y2) {
            return std::nullopt;
        }
        return QStringList{u"M"_s, number(*x1), number(*y1), u"L"_s, number(*x2), number(*y2)}.join(u' ');
    }
    if (name == u"polyline" || name == u"polygon") {
        return pointsPath(a.value(u"points"), name == u"polygon");
    }
    return std::nullopt;
}

}

QString iconPathOfSvg(const QByteArray& svg)
{
    if (svg.isEmpty() || static_cast<std::size_t>(svg.size()) > kMostIconBytes) {
        return {};
    }
    QXmlStreamReader reader(svg);
    QStringList parts;
    bool root = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isDTD() || reader.isEntityReference()) {
            return {};
        }
        if (!reader.isStartElement()) {
            continue;
        }
        const QStringView name = reader.name();
        const QXmlStreamAttributes attributes = reader.attributes();
        if (!root) {
            const QString box = attributes.value(u"viewBox").toString().simplified();
            if (name != u"svg" || (!box.isEmpty() && box != u"0 0 24 24")) {
                return {};
            }
            root = true;
            continue;
        }
        if (attributes.hasAttribute(u"transform")) {
            return {};
        }
        if (name == u"g" || name == u"title" || name == u"desc") {
            continue;
        }
        const std::optional<QString> path = shapePath(name, attributes);
        if (!path) {
            return {};
        }
        parts.append(*path);
    }
    if (reader.hasError() || !root || parts.isEmpty()) {
        return {};
    }
    return parts.join(u' ');
}

QString extensionIconPath(const std::filesystem::path& folder, std::string_view file)
{
    namespace fs = std::filesystem;
    const fs::path relative(std::u8string(reinterpret_cast<const char8_t*>(file.data()), file.size()));
    if (folder.empty() || file.empty() || relative.is_absolute() || relative.has_root_name() ||
        relative.extension() != ".svg" ||
        std::ranges::any_of(relative, [](const fs::path& part) { return part == ".."; })) {
        return {};
    }
    std::error_code error;
    const fs::path full = fs::weakly_canonical(folder / relative, error);
    const fs::path root = fs::weakly_canonical(folder, error);
    if (error || !full.native().starts_with(root.native())) {
        return {};
    }
    static std::mutex mutex;
    static std::map<fs::path, QString> cached;
    static std::map<fs::path, std::size_t> perFolder;
    {
        const std::scoped_lock lock(mutex);
        if (const auto found = cached.find(full); found != cached.end()) {
            return found->second;
        }
        if (perFolder[root] >= kMostIconsPerExtension) {
            return {};
        }
    }
    QFile source(QString::fromStdWString(full.wstring()));
    if (!source.open(QIODevice::ReadOnly) || static_cast<std::size_t>(source.size()) > kMostIconBytes) {
        return {};
    }
    const QString path = iconPathOfSvg(source.readAll());
    const std::scoped_lock lock(mutex);
    if (cached.emplace(full, path).second) {
        ++perFolder[root];
    }
    return path;
}

bool builtinIconName(std::string_view name) noexcept
{
    return !name.empty() && name.size() <= 40 &&
           std::ranges::all_of(name, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

}
