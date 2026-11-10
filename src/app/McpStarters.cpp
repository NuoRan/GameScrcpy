#include "McpStarters.h"

#include <gs/ext/Manifest.h>
#include <gs/script/ApiFiles.h>

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace gs::app {

namespace {

constexpr qsizetype kMostSlugChars = 40;
constexpr qsizetype kMostNameChars = 100;
constexpr qsizetype kMostContributionChars = 64;
static_assert(kMostSlugChars <= static_cast<qsizetype>(ext::kMaxComponentNameLength));

constexpr auto kFilterSource = QLatin1StringView("//! PARAM strength float 0.5 0 1 0.05\n"
                                                 "//! PASS vignette\n"
                                                 "float4 vignette(float2 uv)\n"
                                                 "{\n"
                                                 "    const float4 color = Source.Sample(Linear, uv);\n"
                                                 "    const float2 away = uv - 0.5;\n"
                                                 "    const float dim = saturate(1.0 - strength * 2.0 * dot(away, away));\n"
                                                 "    return float4(color.rgb * dim, color.a);\n"
                                                 "}\n");

constexpr auto kComponentSource = QLatin1StringView("export default class MyComponent extends Component {\n"
                                                    "    static props = {\n"
                                                    "        key: prop.key(\"F\", { label: \"Key\" }),\n"
                                                    "    };\n"
                                                    "\n"
                                                    "    setup() {\n"
                                                    "        this.props.key.onPress(async () => {\n"
                                                    "            const at = this.position;\n"
                                                    "            if (at !== null) {\n"
                                                    "                await touch.tap(at);\n"
                                                    "            }\n"
                                                    "        });\n"
                                                    "    }\n"
                                                    "}\n");

QByteArray jsonBytes(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

QJsonObject manifestOf(const QString& id, const QString& name, const QString& description, const QJsonObject& points,
                       const QJsonObject& contributes)
{
    return {{QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("id"), id},
            {QStringLiteral("version"), QStringLiteral("0.1.0")},
            {QStringLiteral("name"), name},
            {QStringLiteral("description"), description},
            {QStringLiteral("license"), QStringLiteral("Apache-2.0")},
            {QStringLiteral("aiAssisted"), true},
            {QStringLiteral("requires"), points},
            {QStringLiteral("contributes"), contributes}};
}

QJsonObject palette(bool dark)
{
    if (dark) {
        return {{QStringLiteral("bgBase"), QStringLiteral("#101418")},
                {QStringLiteral("bgRaised"), QStringLiteral("#161B21")},
                {QStringLiteral("bgOverlay"), QStringLiteral("#1C222A")},
                {QStringLiteral("bgElevated"), QStringLiteral("#232A33")},
                {QStringLiteral("bgSunken"), QStringLiteral("#0B0E11")},
                {QStringLiteral("textPrimary"), QStringLiteral("#E8EDF2")},
                {QStringLiteral("textSecondary"), QStringLiteral("#A7B1BC")},
                {QStringLiteral("textTertiary"), QStringLiteral("#7F8994")}};
    }
    return {{QStringLiteral("bgBase"), QStringLiteral("#E9EEF2")},
            {QStringLiteral("bgRaised"), QStringLiteral("#F4F7F9")},
            {QStringLiteral("bgOverlay"), QStringLiteral("#FFFFFF")},
            {QStringLiteral("bgElevated"), QStringLiteral("#FFFFFF")},
            {QStringLiteral("bgSunken"), QStringLiteral("#E3E9EE")},
            {QStringLiteral("textPrimary"), QStringLiteral("#10161C")},
            {QStringLiteral("textSecondary"), QStringLiteral("#47525E")},
            {QStringLiteral("textTertiary"), QStringLiteral("#5B6672")}};
}

}

QStringList mcpStarterKinds()
{
    return {QStringLiteral("theme"), QStringLiteral("filter"), QStringLiteral("component")};
}

QString mcpStarterSlug(const QString& name)
{
    QString slug;
    for (const QChar c : name.trimmed().toLower()) {
        if ((c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9')) {
            slug += c;
        } else if (!slug.isEmpty() && !slug.endsWith(u'-')) {
            slug += u'-';
        }
    }
    slug.truncate(kMostSlugChars);
    while (slug.endsWith(u'-')) {
        slug.chop(1);
    }
    if (slug.isEmpty()) {
        slug = QStringLiteral("extension-") +
               QString::fromLatin1(QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256).toHex().left(6));
    }
    return slug;
}

std::optional<McpStarter> mcpStarter(const QString& kind, const QString& name, QString& problem)
{
    const QString title = name.simplified();
    if (title.isEmpty() || title.size() > kMostNameChars) {
        problem = QStringLiteral("name must hold from 1 to %1 characters").arg(kMostNameChars);
        return std::nullopt;
    }
    if (!mcpStarterKinds().contains(kind)) {
        problem = QStringLiteral("kind must be theme, filter or component; native extensions come later");
        return std::nullopt;
    }
    McpStarter starter;
    starter.slug = mcpStarterSlug(title);
    starter.id = QStringLiteral("local.") + starter.slug;
    if (!ext::validExtensionId(starter.id.toStdString())) {
        problem = QStringLiteral("no extension id can be made from the name %1; use Latin letters or digits").arg(title);
        return std::nullopt;
    }
    const QString part = title.left(kMostContributionChars);
    if (kind == u"theme") {
        const QJsonObject theme{{QStringLiteral("name"), part},
                                {QStringLiteral("dark"), palette(true)},
                                {QStringLiteral("light"), palette(false)}};
        starter.files.emplace_back(
            QStringLiteral("manifest.json"),
            jsonBytes(manifestOf(starter.id, title, QStringLiteral("A colour theme for the hub and the mirror windows."),
                                 QJsonObject{{QStringLiteral("theme"), QStringLiteral("^1.0")}},
                                 QJsonObject{{QStringLiteral("themes"), QJsonArray{theme}}})));
        return starter;
    }
    if (kind == u"filter") {
        const QString shader = QStringLiteral("shaders/%1.hlsl").arg(starter.slug);
        QJsonObject manifest = manifestOf(
            starter.id, title, QStringLiteral("A video filter that darkens the edges of the picture."),
            QJsonObject{{QStringLiteral("filter"), QStringLiteral("^1.0")}},
            QJsonObject{{QStringLiteral("filters"),
                         QJsonArray{QJsonObject{{QStringLiteral("name"), part}, {QStringLiteral("file"), shader}}}}});
        manifest.insert(QStringLiteral("settings"),
                        QJsonArray{QJsonObject{{QStringLiteral("key"), QStringLiteral("strength")},
                                               {QStringLiteral("type"), QStringLiteral("number")},
                                               {QStringLiteral("title"), QStringLiteral("Strength")},
                                               {QStringLiteral("min"), 0},
                                               {QStringLiteral("max"), 1},
                                               {QStringLiteral("step"), 0.05},
                                               {QStringLiteral("default"), 0.5},
                                               {QStringLiteral("live"), true}}});
        starter.files.emplace_back(QStringLiteral("manifest.json"), jsonBytes(manifest));
        starter.files.emplace_back(shader, QString(kFilterSource).toUtf8());
        return starter;
    }
    const QString folder = QStringLiteral("components/%1").arg(starter.slug);
    const QJsonObject info{{QStringLiteral("format"), QStringLiteral("gamescrcpy.component")},
                           {QStringLiteral("version"), 1},
                           {QStringLiteral("name"), part},
                           {QStringLiteral("description"), QString()},
                           {QStringLiteral("icon"), QStringLiteral("sparkles")},
                           {QStringLiteral("category"), QString()}};
    starter.files.emplace_back(
        QStringLiteral("manifest.json"),
        jsonBytes(manifestOf(starter.id, title, QStringLiteral("A component made with logic: one key taps where it is placed."),
                             QJsonObject{{QStringLiteral("component"), QStringLiteral("^1.0")}},
                             QJsonObject{{QStringLiteral("components"),
                                          QJsonArray{QJsonObject{{QStringLiteral("name"), starter.slug},
                                                                 {QStringLiteral("folder"), folder}}}}})));
    starter.files.emplace_back(folder + QStringLiteral("/component.ts"), QString(kComponentSource).toUtf8());
    starter.files.emplace_back(folder + QStringLiteral("/component.json"), jsonBytes(info));
    starter.files.emplace_back(folder + QStringLiteral("/tsconfig.json"), QByteArray::fromStdString(script::profileTsconfig()));
    return starter;
}

}
