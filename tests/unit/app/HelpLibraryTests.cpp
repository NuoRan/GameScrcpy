#include "AppSettings.h"
#include "CommandTable.h"
#include "Commands.h"
#include "DeviceCommands.h"
#include "HelpLibrary.h"

#include <gs/script/ApiFiles.h>

#include <doctest/doctest.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyCombination>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QXmlStreamReader>

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using gs::app::AppSettings;
using gs::app::Commands;
using gs::app::CommandScope;
using gs::app::HelpLibrary;
using gs::app::HelpTopic;

namespace {

const QString kLanguage = QStringLiteral("general.language");
const QString kEnglish = QStringLiteral("en");
const QString kChinese = QStringLiteral("zh_CN");

class NullHost final : public gs::app::DeviceCommandHost
{
public:
    [[nodiscard]] std::vector<gs::app::DeviceFacts> devices() const override { return {}; }
    [[nodiscard]] std::optional<gs::app::DeviceFacts> device(const QString&) const override { return std::nullopt; }
    [[nodiscard]] bool canRetryAdb() const override { return false; }

    void retryAdb() override {}
    void connectDevice(const QString&) override {}
    void disconnectDevice(const QString&) override {}
    void showWindow(const QString&) override {}
    void setTransport(const QString&, int) override {}
    void setMuted(const QString&, bool) override {}
    void setVolume(const QString&, double) override {}
    void setScreenOff(const QString&, bool) override {}
    void setOrientation(const QString&, int) override {}
    void pressKey(const QString&, int) override {}
    void setDiagnostics(const QString&, bool) override {}
    void setFullScreen(const QString&, bool) override {}
    void setMini(const QString&, bool) override {}
    void setEditing(const QString&, bool) override {}
    void editorAction(const QString&, const QString&) override {}
    void setMeasuringLatency(const QString&, bool) override {}
    void toggleToolbar(const QString&) override {}
    void setKeymap(const QString&, bool) override {}
    void setCameraLocked(const QString&, bool) override {}
    void setKeyHints(const QString&, bool) override {}
    void stepProfile(const QString&, int) override {}
    void endWireless(const QString&) override {}
};

QString textOf(const QString& path)
{
    QFile file(path);
    REQUIRE_MESSAGE(file.open(QIODevice::ReadOnly), path.toStdString());
    return QString::fromUtf8(file.readAll()).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
}

void write(const QString& path, const QString& contents)
{
    REQUIRE(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray bytes = contents.toUtf8();
    REQUIRE(file.write(bytes) == bytes.size());
}

QJsonObject bundledIndex()
{
    const QJsonDocument document = QJsonDocument::fromJson(textOf(QStringLiteral(":/help/index.json")).toUtf8());
    REQUIRE(document.isObject());
    return document.object();
}

QStringList captured(const QString& text, const QRegularExpression& pattern)
{
    QStringList found;
    for (QRegularExpressionMatchIterator it = pattern.globalMatch(text); it.hasNext();) {
        found.append(it.next().captured(1));
    }
    return found;
}

QStringList listed(const QVariantList& contents, const QString& key)
{
    QStringList values;
    for (const QVariant& entry : contents) {
        const QVariantMap item = entry.toMap();
        if (item.contains(key)) {
            values.append(item.value(key).toString());
        }
    }
    return values;
}

void writeSmallLibrary(const QString& root)
{
    write(root + QStringLiteral("/index.json"), QStringLiteral(R"({"groups": [
        {"title": {"en": "Guides", "zh_CN": "指南"}, "articles": [
            {"slug": "apples", "icon": "smartphone", "title": {"en": "Apples", "zh_CN": "苹果"}},
            {"slug": "pears", "icon": "keyboard", "title": {"en": "Pears", "zh_CN": "梨"}},
            {"slug": "Bad Slug", "icon": "x", "title": "Bad"}]},
        {"title": {"en": "Script API", "zh_CN": "脚本接口"}, "reference": true, "icon": "terminal"}]})"));
    write(root + QStringLiteral("/en/apples.md"), QStringLiteral("Red apples grow on trees.\n\nSee [the pears](help:pears).\n"));
    write(root + QStringLiteral("/zh_CN/apples.md"), QStringLiteral("红苹果长在树上。\n"));
    write(root + QStringLiteral("/en/pears.md"), QStringLiteral("# A heading of its own\n\nGreen pears are sweet.\n"));
    write(root + QStringLiteral("/api/README.md"),
          QStringLiteral("# Script API\n\n- [`touch`](touch.md): taps.\n- [Globals](globals.md)\n"));
    write(root + QStringLiteral("/api/touch.md"),
          QStringLiteral("# touch\n\nTaps on the picture. Back to the [overview](README.md#top).\n"));
    write(root + QStringLiteral("/api/globals.md"), QStringLiteral("# Global functions and values\n\nsleep\n"));
    write(root + QStringLiteral("/api/zebra.md"), QStringLiteral("A page nobody links to.\n"));
}

std::map<QString, QString> finishedTranslations(const QString& path, const QString& context)
{
    QFile file(path);
    REQUIRE_MESSAGE(file.open(QIODevice::ReadOnly), path.toStdString());
    QXmlStreamReader xml(&file);
    std::map<QString, QString> found;
    QString name;
    QString source;
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }
        if (xml.name() == u"name") {
            name = xml.readElementText();
        } else if (xml.name() == u"source") {
            source = xml.readElementText();
        } else if (xml.name() == u"translation") {
            const bool unfinished = xml.attributes().value(u"type") == u"unfinished";
            const QString text = xml.readElementText(QXmlStreamReader::IncludeChildElements);
            if (name == context && !unfinished && !text.isEmpty()) {
                found.emplace(source, text);
            }
        }
    }
    REQUIRE_FALSE(xml.hasError());
    return found;
}

}

TEST_CASE("every help article in the index is bundled in English and in Chinese, and no bundled article is left out (REL-003)")
{
    const QString icons = textOf(QStringLiteral(GS_APP_TEST_DATA_DIR "/../../../src/ui/icons/IconPaths.js"));
    const auto knownIcon = [&icons](const QString& name) {
        return !name.isEmpty() && icons.contains(u'"' + name + QStringLiteral("\":"));
    };
    QStringList slugs;
    for (const QJsonValue& groupValue : bundledIndex().value(QStringLiteral("groups")).toArray()) {
        const QJsonObject group = groupValue.toObject();
        const QJsonObject groupTitle = group.value(QStringLiteral("title")).toObject();
        CHECK_FALSE(groupTitle.value(kEnglish).toString().isEmpty());
        CHECK_FALSE(groupTitle.value(kChinese).toString().isEmpty());
        if (group.value(QStringLiteral("reference")).toBool()) {
            CHECK(knownIcon(group.value(QStringLiteral("icon")).toString()));
            continue;
        }
        for (const QJsonValue& articleValue : group.value(QStringLiteral("articles")).toArray()) {
            const QJsonObject article = articleValue.toObject();
            const QString slug = article.value(QStringLiteral("slug")).toString();
            INFO(slug.toStdString());
            const QJsonObject title = article.value(QStringLiteral("title")).toObject();
            CHECK_FALSE(title.value(kEnglish).toString().isEmpty());
            CHECK_FALSE(title.value(kChinese).toString().isEmpty());
            CHECK(knownIcon(article.value(QStringLiteral("icon")).toString()));
            slugs.append(slug);
        }
    }
    CHECK(slugs == QStringList{QStringLiteral("connecting"), QStringLiteral("aoa-driver"), QStringLiteral("touch-box"),
                               QStringLiteral("capture-card"), QStringLiteral("mirror-window"),
                               QStringLiteral("keymap-editor"), QStringLiteral("profiles"), QStringLiteral("scripts"),
                               QStringLiteral("tours"), QStringLiteral("companion"), QStringLiteral("extensions"),
                               QStringLiteral("extension-features"), QStringLiteral("faq")});
    for (const QString& language : {kEnglish, kChinese}) {
        for (const QString& slug : slugs) {
            const QString path = QStringLiteral(":/help/%1/%2.md").arg(language, slug);
            CHECK_MESSAGE(QFile::exists(path), path.toStdString());
            CHECK_FALSE_MESSAGE(textOf(path).trimmed().isEmpty(), path.toStdString());
        }
        for (const QString& file :
             QDir(QStringLiteral(":/help/") + language).entryList({QStringLiteral("*.md")}, QDir::Files, QDir::Name)) {
            CHECK_MESSAGE(slugs.contains(file.chopped(3)), (language + u'/' + file).toStdString());
        }
    }
}

TEST_CASE("every help: link in the articles and the script API reference opens a bundled page and section (REL-003)")
{
    const QTemporaryDir data;
    REQUIRE(data.isValid());
    const AppSettings settings(data.path().toStdWString());
    const HelpLibrary help(settings);
    REQUIRE_FALSE(help.topics().empty());

    const auto hasAnchor = [&help](const QString& slug, const QString& anchor) {
        const HelpTopic* topic = help.find(slug);
        if (topic == nullptr) {
            return false;
        }
        for (const auto& [language, document] : topic->documents) {
            const bool found = std::ranges::any_of(document.searchable, [&anchor](const gs::app::HelpSection& section) {
                return section.anchor == anchor;
            });
            if (!found) {
                return false;
            }
        }
        return true;
    };
    static const QRegularExpression link(QStringLiteral(R"((?<!!)\[[^\]]*\]\(\s*([^)\s]+))"));
    int opened = 0;
    int sections = 0;
    for (const HelpTopic& topic : help.topics()) {
        for (const auto& [language, text] : topic.texts) {
            for (const QString& target : captured(text, link)) {
                INFO(topic.slug.toStdString(), " ", language.toStdString(), " ", target.toStdString());
                if (target.startsWith(QStringLiteral("help:"))) {
                    ++opened;
                    const QString slug = target.mid(5).section(u'#', 0, 0);
                    const QString anchor = target.section(u'#', 1);
                    CHECK(help.contains(slug));
                    if (!anchor.isEmpty()) {
                        ++sections;
                        CHECK(hasAnchor(slug, anchor));
                    }
                } else if (target.startsWith(QStringLiteral("action:tour:"))) {
                    CHECK(QStringList{QStringLiteral("devices"), QStringLiteral("mirror"), QStringLiteral("keymap"),
                                      QStringLiteral("logic"), QStringLiteral("profiles"), QStringLiteral("tasks"),
                                      QStringLiteral("extensions"), QStringLiteral("settings"),
                                      QStringLiteral("assistant"), QStringLiteral("all")}
                              .contains(target.mid(12)));
                } else if (target.startsWith(QStringLiteral("action:"))) {
                    CHECK(target == QStringLiteral("action:guide"));
                } else {
                    CHECK((target.startsWith(QStringLiteral("https://")) || target.startsWith(QStringLiteral("http://"))));
                }
            }
        }
    }
    CHECK(opened > 0);
    CHECK(sections > 0);
    CHECK(help.article(QStringLiteral("scripts"), kEnglish).value(QStringLiteral("text")).toString().contains(
        QStringLiteral("(help:api)")));
    for (const QJsonValue& entry : bundledIndex().value(QStringLiteral("popular")).toArray()) {
        INFO(entry.toString().toStdString());
        CHECK(hasAnchor(entry.toString().section(u'#', 0, 0), entry.toString().section(u'#', 1)));
    }
}

TEST_CASE("every picture in a help article is bundled with it (REL-003)")
{
    static const QRegularExpression image(QStringLiteral(R"(!\[[^\]]*\]\(\s*([^)\s]+))"));
    CHECK(captured(QStringLiteral("Look: ![The toolbar](images/toolbar.png) and [a link](help:faq)"), image) ==
          QStringList{QStringLiteral("images/toolbar.png")});

    const QTemporaryDir data;
    REQUIRE(data.isValid());
    const AppSettings settings(data.path().toStdWString());
    const HelpLibrary help(settings);
    CHECK(help.baseUrl() == QUrl(QStringLiteral("qrc:/help/")));
    for (const HelpTopic& topic : help.topics()) {
        for (const auto& [language, text] : topic.texts) {
            for (const QString& picture : captured(text, image)) {
                INFO(topic.slug.toStdString(), " ", language.toStdString(), " ", picture.toStdString());
                if (picture.startsWith(QStringLiteral("figure:"))) {
                    continue;
                }
                CHECK_FALSE(picture.contains(u':'));
                CHECK(QFile::exists(QStringLiteral(":/help/") + picture));
            }
        }
    }
}

TEST_CASE("every figure and icon a help article names is one the help page can draw (ADR-0090)")
{
    const QString figures = textOf(QStringLiteral(GS_APP_TEST_DATA_DIR "/../../../src/app/qml/HelpFigure.qml"));
    const QStringList drawn = captured(figures, QRegularExpression(QStringLiteral(R"re(case "([a-z0-9-]+)":)re")));
    REQUIRE(drawn.contains(QStringLiteral("phone-allow")));
    const QString icons = textOf(QStringLiteral(GS_APP_TEST_DATA_DIR "/../../../src/ui/icons/IconPaths.js"));
    const auto knownIcon = [&icons](const QString& name) { return icons.contains(u'"' + name + QStringLiteral("\":")); };

    const QTemporaryDir data;
    REQUIRE(data.isValid());
    const AppSettings settings(data.path().toStdWString());
    const HelpLibrary help(settings);

    QStringList named;
    QStringList iconNames;
    std::function<void(const QVariantList&)> walk = [&](const QVariantList& blocks) {
        for (const QVariant& value : blocks) {
            const QVariantMap block = value.toMap();
            const QString kind = block.value(QStringLiteral("kind")).toString();
            if (kind == QStringLiteral("figure")) {
                named += block.value(QStringLiteral("names")).toStringList();
                for (const QVariant& entry : block.value(QStringLiteral("legend")).toList()) {
                    iconNames.append(entry.toMap().value(QStringLiteral("icon")).toString());
                }
            } else if (kind == QStringLiteral("steps")) {
                named += block.value(QStringLiteral("figure")).toMap().value(QStringLiteral("names")).toStringList();
            } else if (kind == QStringLiteral("choices")) {
                for (const QVariant& entry : block.value(QStringLiteral("items")).toList()) {
                    iconNames.append(entry.toMap().value(QStringLiteral("icon")).toString());
                }
            } else if (kind == QStringLiteral("heading")) {
                iconNames.append(block.value(QStringLiteral("icon")).toString());
            }
        }
    };
    int setups = 0;
    for (const HelpTopic& topic : help.topics()) {
        if (!topic.hero.isEmpty()) {
            named.append(topic.hero);
        }
        for (const auto& [language, document] : topic.documents) {
            walk(document.blocks);
            setups += static_cast<int>(std::ranges::count_if(document.blocks, [](const QVariant& block) {
                return block.toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("setup");
            }));
        }
    }
    CHECK(setups == 2);
    CHECK(named.size() > 20);
    for (const QString& name : named) {
        INFO(name.toStdString());
        CHECK(drawn.contains(name));
    }
    for (const QString& name : iconNames) {
        INFO(name.toStdString());
        CHECK((name.isEmpty() || knownIcon(name)));
    }
}

TEST_CASE("the script API reference from sdk/docs is bundled under its own group, overview first, with help: links (REL-003)")
{
    const QDir sdk(QStringLiteral(GS_APP_TEST_DATA_DIR "/../../../sdk/docs"));
    REQUIRE(sdk.exists());
    const QStringList pages = sdk.entryList({QStringLiteral("*.md")}, QDir::Files, QDir::Name);
    REQUIRE(pages.contains(QStringLiteral("README.md")));
    for (const QString& page : pages) {
        CHECK_MESSAGE(textOf(QStringLiteral(":/help/api/") + page) == textOf(sdk.filePath(page)), page.toStdString());
    }

    const QTemporaryDir data;
    REQUIRE(data.isValid());
    AppSettings settings(data.path().toStdWString());
    REQUIRE(settings.setValue(kLanguage, kChinese));
    const HelpLibrary help(settings);
    std::vector<const HelpTopic*> reference;
    for (const HelpTopic& topic : help.topics()) {
        if (topic.reference) {
            reference.push_back(&topic);
        }
    }
    REQUIRE(reference.size() == static_cast<std::size_t>(pages.size()));
    CHECK(reference.front()->slug == QStringLiteral("api"));
    CHECK(reference.front()->group + 1 == help.groups().size());
    CHECK(help.topics().back().reference);

    const QVariantMap touch = help.article(QStringLiteral("api/touch"));
    CHECK(touch.value(QStringLiteral("title")).toString() == QStringLiteral("touch"));
    CHECK(touch.value(QStringLiteral("group")).toString() == QStringLiteral("脚本接口"));
    CHECK(touch.value(QStringLiteral("reference")).toBool());
    CHECK(touch.value(QStringLiteral("text")).toString().contains(QStringLiteral("touch.tap(")));
    CHECK_FALSE(touch.value(QStringLiteral("text")).toString().startsWith(QStringLiteral("# ")));
    CHECK(touch.value(QStringLiteral("text")) == help.article(QStringLiteral("api/touch"), kEnglish).value(QStringLiteral("text")));
    const QString overview = help.article(QStringLiteral("api")).value(QStringLiteral("text")).toString();
    CHECK(overview.contains(QStringLiteral("(help:api/touch)")));
    CHECK_FALSE(overview.contains(QStringLiteral(".md)")));
    CHECK(listed(help.contents(), QStringLiteral("label")).contains(QStringLiteral("脚本接口")));
}

TEST_CASE("help shows the Chinese articles for a Chinese interface, follows a language change and falls back to English (REL-003)")
{
    const QTemporaryDir data;
    const QTemporaryDir root;
    REQUIRE(data.isValid());
    REQUIRE(root.isValid());
    writeSmallLibrary(root.path());
    AppSettings settings(data.path().toStdWString());
    REQUIRE(settings.setValue(kLanguage, kChinese));
    HelpLibrary help(settings, root.path());
    int languages = 0;
    int contents = 0;
    QObject::connect(&help, &HelpLibrary::languageChanged, &help, [&languages] { ++languages; });
    QObject::connect(&help, &HelpLibrary::contentsChanged, &help, [&contents] { ++contents; });

    CHECK(help.language() == kChinese);
    CHECK(help.firstArticle() == QStringLiteral("apples"));
    CHECK_FALSE(help.contains(QStringLiteral("Bad Slug")));
    const QVariantMap apples = help.article(QStringLiteral("apples"));
    CHECK(apples.value(QStringLiteral("title")).toString() == QStringLiteral("苹果"));
    CHECK(apples.value(QStringLiteral("group")).toString() == QStringLiteral("指南"));
    CHECK(apples.value(QStringLiteral("icon")).toString() == QStringLiteral("smartphone"));
    CHECK(apples.value(QStringLiteral("text")).toString() == QStringLiteral("红苹果长在树上。\n"));
    CHECK(apples.value(QStringLiteral("baseUrl")).toUrl() ==
          QUrl::fromLocalFile(QDir(root.path()).absolutePath() + u'/'));
    const QVariantMap pears = help.article(QStringLiteral("pears"));
    CHECK(pears.value(QStringLiteral("title")).toString() == QStringLiteral("梨"));
    CHECK(pears.value(QStringLiteral("text")).toString() == QStringLiteral("Green pears are sweet.\n"));
    CHECK(help.article(QStringLiteral("missing")).isEmpty());
    CHECK(listed(help.contents(), QStringLiteral("title")) ==
          QStringList{QStringLiteral("苹果"), QStringLiteral("梨"), QStringLiteral("Script API"), QStringLiteral("touch"),
                      QStringLiteral("Global functions and values"), QStringLiteral("zebra")});
    CHECK(listed(help.contents(), QStringLiteral("slug")) ==
          QStringList{QStringLiteral("apples"), QStringLiteral("pears"), QStringLiteral("api"), QStringLiteral("api/touch"),
                      QStringLiteral("api/globals"), QStringLiteral("api/zebra")});
    CHECK(help.article(QStringLiteral("api")).value(QStringLiteral("text")).toString() ==
          QStringLiteral("- [`touch`](help:api/touch): taps.\n- [Globals](help:api/globals)\n"));
    CHECK(help.article(QStringLiteral("api/touch")).value(QStringLiteral("text")).toString() ==
          QStringLiteral("Taps on the picture. Back to the [overview](help:api).\n"));

    REQUIRE(settings.setValue(kLanguage, kEnglish));
    CHECK(languages == 1);
    CHECK(contents == 1);
    CHECK(help.language() == kEnglish);
    CHECK(help.article(QStringLiteral("apples")).value(QStringLiteral("text")).toString() ==
          QStringLiteral("Red apples grow on trees.\n\nSee [the pears](help:pears).\n"));
    CHECK(listed(help.contents(), QStringLiteral("label")) == QStringList{QStringLiteral("Guides"), QStringLiteral("Script API")});
    CHECK(help.article(QStringLiteral("apples"), kChinese).value(QStringLiteral("text")).toString() ==
          QStringLiteral("红苹果长在树上。\n"));
    REQUIRE(settings.setValue(kLanguage, kEnglish));
    CHECK(languages == 1);
}

TEST_CASE("the help search narrows the table of contents by title and text, in the interface language (REL-003)")
{
    const QTemporaryDir data;
    const QTemporaryDir root;
    REQUIRE(data.isValid());
    REQUIRE(root.isValid());
    writeSmallLibrary(root.path());
    AppSettings settings(data.path().toStdWString());
    REQUIRE(settings.setValue(kLanguage, kEnglish));
    HelpLibrary help(settings, root.path());
    int queries = 0;
    int contents = 0;
    QObject::connect(&help, &HelpLibrary::queryChanged, &help, [&queries] { ++queries; });
    QObject::connect(&help, &HelpLibrary::contentsChanged, &help, [&contents] { ++contents; });

    help.setQuery(QStringLiteral("green"));
    CHECK(listed(help.contents(), QStringLiteral("slug")) == QStringList{QStringLiteral("pears")});
    CHECK(listed(help.contents(), QStringLiteral("label")) == QStringList{QStringLiteral("Guides")});
    help.setQuery(QStringLiteral("  green "));
    CHECK(queries == 2);
    CHECK(contents == 1);
    help.setQuery(QStringLiteral("TREES"));
    CHECK(listed(help.contents(), QStringLiteral("slug")) == QStringList{QStringLiteral("apples")});
    help.setQuery(QStringLiteral("sweet green"));
    CHECK(listed(help.contents(), QStringLiteral("slug")) == QStringList{QStringLiteral("pears")});
    help.setQuery(QStringLiteral("sweet trees"));
    CHECK(help.contents().isEmpty());
    help.setQuery(QStringLiteral("help"));
    CHECK(help.contents().isEmpty());
    help.setQuery(QStringLiteral("taps"));
    CHECK(listed(help.contents(), QStringLiteral("slug")) == QStringList{QStringLiteral("api"), QStringLiteral("api/touch")});
    CHECK(listed(help.contents(), QStringLiteral("label")) == QStringList{QStringLiteral("Script API")});

    REQUIRE(settings.setValue(kLanguage, kChinese));
    help.setQuery(QStringLiteral("苹果"));
    CHECK(listed(help.contents(), QStringLiteral("slug")) == QStringList{QStringLiteral("apples")});
    help.setQuery(QStringLiteral("树上"));
    CHECK(listed(help.contents(), QStringLiteral("slug")) == QStringList{QStringLiteral("apples")});
    help.setQuery(QStringLiteral("trees"));
    CHECK(help.contents().isEmpty());
    help.setQuery(QStringLiteral("green"));
    CHECK(listed(help.contents(), QStringLiteral("slug")) == QStringList{QStringLiteral("pears")});
    help.setQuery({});
    CHECK(listed(help.contents(), QStringLiteral("slug")).size() == 6);
}

TEST_CASE("the bundled help reads in Chinese for a Chinese interface and its search finds pairing in both languages (REL-003)")
{
    const QTemporaryDir data;
    REQUIRE(data.isValid());
    AppSettings settings(data.path().toStdWString());
    REQUIRE(settings.setValue(kLanguage, kChinese));
    HelpLibrary help(settings);

    CHECK(help.firstArticle() == QStringLiteral("connecting"));
    const QVariantMap chinese = help.article(QStringLiteral("connecting"));
    CHECK(chinese.value(QStringLiteral("title")).toString() == QStringLiteral("连接手机"));
    CHECK(chinese.value(QStringLiteral("text")).toString() == textOf(QStringLiteral(":/help/zh_CN/connecting.md")));
    help.setQuery(QStringLiteral("配对码"));
    CHECK(listed(help.contents(), QStringLiteral("slug")).contains(QStringLiteral("connecting")));

    REQUIRE(settings.setValue(kLanguage, kEnglish));
    const QVariantMap english = help.article(QStringLiteral("connecting"));
    CHECK(english.value(QStringLiteral("title")).toString() == QStringLiteral("Connecting a phone"));
    CHECK(english.value(QStringLiteral("text")).toString() == textOf(QStringLiteral(":/help/en/connecting.md")));
    help.setQuery(QStringLiteral("pairing code"));
    CHECK(listed(help.contents(), QStringLiteral("slug")).contains(QStringLiteral("connecting")));
}

TEST_CASE("the help command answers F1 in every window and carries the article to open (REL-003, ADR-0090)")
{
    NullHost host;
    Commands commands(host);
    const QString id = QStringLiteral("app.help");
    const QKeyCombination f1(Qt::Key_F1);

    const gs::app::Command* help = commands.table().find(id);
    REQUIRE(help != nullptr);
    CHECK(help->scope == CommandScope::Global);
    CHECK(help->target == gs::app::CommandTarget::Topic);
    CHECK(help->shortcut == f1);
    CHECK(commands.title(id) == QStringLiteral("Help"));
    CHECK(commands.icon(id) == QStringLiteral("circle-help"));
    CHECK(commands.shortcutText(id) == QStringLiteral("F1"));
    CHECK(commands.match(f1, CommandScope::Hub) == id);
    CHECK(commands.match(f1, CommandScope::Mirror) == id);

    QStringList requests;
    QObject::connect(&commands, &Commands::uiRequested, &commands,
                     [&requests](const QString& command, const QString& target) { requests.append(command + u' ' + target); });
    CHECK(commands.trigger(id));
    CHECK(commands.trigger(id, QStringLiteral("keymap-editor")));
    CHECK(requests == QStringList{QStringLiteral("app.help "), QStringLiteral("app.help keymap-editor")});
}

TEST_CASE("the help home lists the guides with their look, the popular questions and the script API pages (ADR-0090)")
{
    const QTemporaryDir data;
    REQUIRE(data.isValid());
    AppSettings settings(data.path().toStdWString());
    REQUIRE(settings.setValue(kLanguage, kChinese));
    const HelpLibrary help(settings);

    const QVariantMap home = help.home();
    const QVariantList guides = home.value(QStringLiteral("guides")).toList();
    REQUIRE(guides.size() == 13);
    const QVariantMap connecting = guides.front().toMap();
    CHECK(connecting.value(QStringLiteral("slug")).toString() == QStringLiteral("connecting"));
    CHECK(connecting.value(QStringLiteral("title")).toString() == QStringLiteral("连接手机"));
    CHECK(connecting.value(QStringLiteral("tile")).toString() == QStringLiteral("azure"));
    CHECK(connecting.value(QStringLiteral("layout")).toString() == QStringLiteral("guide"));
    CHECK_FALSE(connecting.value(QStringLiteral("summary")).toString().isEmpty());
    CHECK(connecting.value(QStringLiteral("count")).toInt() == 7);
    CHECK(connecting.value(QStringLiteral("minutes")).toInt() >= 2);
    const QVariantMap faq = guides.back().toMap();
    CHECK(faq.value(QStringLiteral("layout")).toString() == QStringLiteral("faq"));
    CHECK(faq.value(QStringLiteral("count")).toInt() == 18);
    CHECK(faq.value(QStringLiteral("groups")).toInt() == 4);
    for (const QVariant& guide : guides) {
        CHECK_FALSE(guide.toMap().value(QStringLiteral("tile")).toString().isEmpty());
    }

    const QVariantList popular = home.value(QStringLiteral("popular")).toList();
    REQUIRE(popular.size() == 4);
    CHECK(popular.front().toMap() == QVariantMap{{QStringLiteral("slug"), QStringLiteral("faq")},
                                                 {QStringLiteral("anchor"), QStringLiteral("no-phone")},
                                                 {QStringLiteral("title"), QStringLiteral("“设备”页里没有出现手机")}});
    const QVariantList reference = home.value(QStringLiteral("reference")).toList();
    REQUIRE_FALSE(reference.isEmpty());
    CHECK(reference.front().toMap().value(QStringLiteral("slug")).toString() == QStringLiteral("api"));
    CHECK(listed(reference, QStringLiteral("slug")).contains(QStringLiteral("api/touch")));
}

TEST_CASE("an article gives its layout, blocks, sections, neighbours and link cards with the linked article's look (ADR-0090)")
{
    const QTemporaryDir data;
    REQUIRE(data.isValid());
    AppSettings settings(data.path().toStdWString());
    REQUIRE(settings.setValue(kLanguage, kEnglish));
    const HelpLibrary help(settings);

    const QVariantMap connecting = help.article(QStringLiteral("connecting"));
    CHECK(connecting.value(QStringLiteral("layout")).toString() == QStringLiteral("guide"));
    CHECK(connecting.value(QStringLiteral("hero")).toString() == QStringLiteral("hero-connecting"));
    CHECK(connecting.value(QStringLiteral("lead")).toString().startsWith(QStringLiteral("GameScrcpy shows")));
    CHECK(connecting.value(QStringLiteral("previous")).toMap().isEmpty());
    CHECK(connecting.value(QStringLiteral("next")).toMap().value(QStringLiteral("slug")).toString() ==
          QStringLiteral("aoa-driver"));
    CHECK(connecting.value(QStringLiteral("meta")).toList().size() == 1);
    const QVariantList sections = connecting.value(QStringLiteral("sections")).toList();
    REQUIRE(sections.size() == 7);
    CHECK(sections.front().toMap().value(QStringLiteral("title")).toString() == QStringLiteral("Prepare the phone"));
    const QVariantList blocks = connecting.value(QStringLiteral("blocks")).toList();
    QStringList kinds;
    QVariantMap card;
    QVariantMap tabs;
    for (const QVariant& value : blocks) {
        const QVariantMap block = value.toMap();
        kinds.append(block.value(QStringLiteral("kind")).toString());
        if (kinds.back() == QStringLiteral("link")) {
            card = block;
        } else if (kinds.back() == QStringLiteral("tabs")) {
            tabs = block;
        }
    }
    CHECK(kinds.front() == QStringLiteral("heading"));
    CHECK(kinds.contains(QStringLiteral("setup")));
    CHECK(kinds.contains(QStringLiteral("callout")));
    CHECK(kinds.contains(QStringLiteral("choices")));
    CHECK(card.value(QStringLiteral("target")).toString() == QStringLiteral("help:faq#no-phone"));
    CHECK(card.value(QStringLiteral("icon")).toString() == QStringLiteral("circle-help"));
    CHECK(card.value(QStringLiteral("tile")).toString() == QStringLiteral("amber"));
    const QVariantList tabList = tabs.value(QStringLiteral("tabs")).toList();
    REQUIRE(tabList.size() == 2);
    CHECK(tabList.front().toMap().value(QStringLiteral("icon")).toString() == QStringLiteral("type"));
    CHECK(tabList.back().toMap().value(QStringLiteral("icon")).toString() == QStringLiteral("qr-code"));

    CHECK(help.article(QStringLiteral("mirror-window")).value(QStringLiteral("previous")).toMap().value(QStringLiteral("slug")) ==
          QStringLiteral("capture-card"));
    CHECK(help.article(QStringLiteral("capture-card")).value(QStringLiteral("previous")).toMap().value(QStringLiteral("slug")) ==
          QStringLiteral("touch-box"));
    CHECK(help.article(QStringLiteral("touch-box")).value(QStringLiteral("previous")).toMap().value(QStringLiteral("slug")) ==
          QStringLiteral("aoa-driver"));
    CHECK(help.article(QStringLiteral("faq")).value(QStringLiteral("next")).toMap().isEmpty());

    const QVariantMap faq = help.article(QStringLiteral("faq"));
    CHECK(faq.value(QStringLiteral("layout")).toString() == QStringLiteral("faq"));
    CHECK(faq.value(QStringLiteral("blocks")).toList().isEmpty());
    const QVariantList categories = faq.value(QStringLiteral("categories")).toList();
    REQUIRE(categories.size() == 4);
    CHECK(categories.front().toMap().value(QStringLiteral("tone")).toString() == QStringLiteral("azure"));
    CHECK(faq.value(QStringLiteral("sections")).toList().size() == 4);

    const QVariantMap touch = help.article(QStringLiteral("api/touch"));
    CHECK(touch.value(QStringLiteral("layout")).toString() == QStringLiteral("reference"));
    CHECK(std::ranges::any_of(touch.value(QStringLiteral("blocks")).toList(), [](const QVariant& block) {
        return block.toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("function");
    }));
}

TEST_CASE("the help search lists the matching sections under each article, at most five (ADR-0090)")
{
    const QTemporaryDir data;
    REQUIRE(data.isValid());
    AppSettings settings(data.path().toStdWString());
    REQUIRE(settings.setValue(kLanguage, kChinese));
    HelpLibrary help(settings);

    help.setQuery(QStringLiteral("配对码"));
    std::map<QString, int> perArticle;
    QStringList anchors;
    for (const QVariant& value : help.contents()) {
        const QVariantMap entry = value.toMap();
        if (entry.value(QStringLiteral("kind")).toString() == QStringLiteral("section")) {
            ++perArticle[entry.value(QStringLiteral("of")).toString()];
            anchors.append(entry.value(QStringLiteral("anchor")).toString());
        }
    }
    CHECK(perArticle[QStringLiteral("connecting")] >= 1);
    for (const auto& [slug, count] : perArticle) {
        CHECK(count <= HelpLibrary::kMostSectionHits);
    }
    CHECK(anchors.contains(QStringLiteral("s3-1")));
    help.setQuery({});
    CHECK(listed(help.contents(), QStringLiteral("of")).isEmpty());
}

TEST_CASE("the script API reference reads in Chinese for a Chinese interface, names, types and examples as written (ADR-0065)")
{
    const QTemporaryDir data;
    REQUIRE(data.isValid());
    AppSettings settings(data.path().toStdWString());
    REQUIRE(settings.setValue(kLanguage, kChinese));
    const HelpLibrary help(settings, QStringLiteral(":/help"), [](const QString& language, const QString& text) {
        return language == kChinese ? QStringLiteral("译 ") + text : QString();
    });

    const QVariantMap touch = help.article(QStringLiteral("api/touch"));
    CHECK(touch.value(QStringLiteral("title")).toString() == QStringLiteral("touch"));
    CHECK(touch.value(QStringLiteral("lead")).toString().startsWith(QStringLiteral("译 Touches on the phone picture.")));
    QVariantMap tap;
    for (const QVariant& block : touch.value(QStringLiteral("blocks")).toList()) {
        if (block.toMap().value(QStringLiteral("title")).toString() == QStringLiteral("touch.tap(x, y?, durationMs?)")) {
            tap = block.toMap();
        }
    }
    REQUIRE(tap.value(QStringLiteral("kind")).toString() == QStringLiteral("function"));
    CHECK(tap.value(QStringLiteral("html")).toString().startsWith(QStringLiteral("译 Taps the picture at a position;")));
    CHECK(tap.value(QStringLiteral("signature")).toString() ==
          QStringLiteral("touch.tap(x: number | PicturePoint, y?: number, durationMs?: number): Action<void>"));
    CHECK(tap.value(QStringLiteral("stability")).toString() == QStringLiteral("stable"));
    CHECK(tap.value(QStringLiteral("example")).toString().startsWith(QStringLiteral("await touch.tap(0.8, 0.9)")));
    const QVariantList params = tap.value(QStringLiteral("params")).toList();
    REQUIRE(params.size() == 3);
    CHECK(params[0].toMap().value(QStringLiteral("name")).toString() == QStringLiteral("x"));
    CHECK(params[0].toMap().value(QStringLiteral("type")).toString() == QStringLiteral("number | PicturePoint"));
    CHECK(params[0].toMap().value(QStringLiteral("html")).toString().startsWith(QStringLiteral("译 Where to tap")));
    CHECK(params[2].toMap().value(QStringLiteral("optional")).toBool());
    const QVariantList failures = tap.value(QStringLiteral("failures")).toList();
    REQUIRE_FALSE(failures.isEmpty());
    CHECK(failures[0].toMap().value(QStringLiteral("code")).toString() == QStringLiteral("GS-API-002"));
    CHECK(failures[0].toMap().value(QStringLiteral("html")).toString() == QStringLiteral("译 a position is outside 0 to 1"));
    CHECK(failures[0].toMap().value(QStringLiteral("fix")).toString() ==
          QStringLiteral("译 Positions are picture ratios; divide pixels by the picture size."));

    CHECK(help.article(QStringLiteral("api/touch"), kEnglish)
              .value(QStringLiteral("lead"))
              .toString()
              .startsWith(QStringLiteral("Touches on the phone picture.")));
    CHECK(help.article(QStringLiteral("api")).value(QStringLiteral("title")).toString() ==
          QStringLiteral("译 GameScrcpy script API"));
    CHECK(help.article(QStringLiteral("api"), kEnglish).value(QStringLiteral("title")).toString() ==
          QStringLiteral("GameScrcpy script API"));
    CHECK(listed(help.contents(), QStringLiteral("title")).contains(QStringLiteral("译 Global functions and values")));
}

TEST_CASE("every text of the script API reference has a finished Chinese translation, and the reference shows it (ADR-0065)")
{
    const std::map<QString, QString> chinese = finishedTranslations(
        QStringLiteral(GS_APP_TEST_DATA_DIR "/../../../src/app/i18n/gamescrcpy_zh_CN.ts"), QStringLiteral("ScriptApi"));
    int missing = 0;
    for (const std::string& text : gs::script::apiTexts()) {
        INFO(text);
        const bool found = chinese.contains(QString::fromStdString(text));
        CHECK(found);
        missing += found ? 0 : 1;
    }
    REQUIRE(missing == 0);

    const QTemporaryDir data;
    REQUIRE(data.isValid());
    AppSettings settings(data.path().toStdWString());
    REQUIRE(settings.setValue(kLanguage, kChinese));
    const HelpLibrary help(settings, QStringLiteral(":/help"), [&chinese](const QString& language, const QString& text) {
        const auto found = chinese.find(text);
        return language == kChinese && found != chinese.end() ? found->second : QString();
    });
    const QString lead = QStringLiteral(
        "Touches on the phone picture. Positions are ratios of the picture: (0, 0) top left, (1, 1) bottom right.");
    CHECK(help.article(QStringLiteral("api/touch")).value(QStringLiteral("lead")).toString() ==
          gs::app::helpInline(chinese.at(lead)));
    CHECK(help.article(QStringLiteral("api")).value(QStringLiteral("title")).toString() ==
          chinese.at(QStringLiteral("GameScrcpy script API")));
}

TEST_CASE("ADR-0112: extensions add help articles under their own group, found by the search, with pictures only from "
          "their own folder")
{
    const QTemporaryDir data;
    const QTemporaryDir root;
    REQUIRE(data.isValid());
    REQUIRE(root.isValid());
    writeSmallLibrary(root.path());
    AppSettings settings(data.path().toStdWString());
    REQUIRE(settings.setValue(kLanguage, kEnglish));
    HelpLibrary help(settings, root.path(), [](const QString& language, const QString& text) {
        return language == kChinese && text == QStringLiteral("Extensions") ? QStringLiteral("扩展") : QString();
    });
    int contents = 0;
    QObject::connect(&help, &HelpLibrary::contentsChanged, &help, [&contents] { ++contents; });
    const QUrl folder = QUrl::fromLocalFile(QDir(root.path()).absolutePath() + QStringLiteral("/boards/"));
    const QString setupSlug = QStringLiteral("ext/alice.boards/setup");
    const QString faqSlug = QStringLiteral("ext/alice.boards/faq");
    help.setExtensionTopics(
        {{.extension = QStringLiteral("alice.boards"),
          .extensionName = QStringLiteral("Alice boards"),
          .id = QStringLiteral("setup"),
          .titles = {{kEnglish, QStringLiteral("Setting up")}, {kChinese, QStringLiteral("设置")}},
          .texts = {{kEnglish, QStringLiteral("# Setting up\n\nPlug the board into the phone.\n\n![The board](images/board.png)\n\n"
                                              "![Away](../outside.png)\n\n![Web](https://example.com/a.png)\n")},
                    {kChinese, QStringLiteral("把板子插到手机上。\n")}},
          .icon = {},
          .iconPath = QStringLiteral("M2 2 L22 22"),
          .base = folder},
         {.extension = QStringLiteral("alice.boards"),
          .extensionName = QStringLiteral("Alice boards"),
          .id = QStringLiteral("faq"),
          .titles = {{kEnglish, QStringLiteral("Questions")}},
          .texts = {{kEnglish, QStringLiteral("Why does the light blink?\n")}},
          .icon = QStringLiteral("circle-help"),
          .iconPath = {},
          .base = folder},
         {.extension = QStringLiteral("bob.tools"),
          .extensionName = QStringLiteral("Bob tools"),
          .id = QStringLiteral("intro"),
          .titles = {{kEnglish, QStringLiteral("Intro")}},
          .texts = {{kEnglish, QStringLiteral("Hello.\n")}}}});
    CHECK(contents == 1);
    CHECK(listed(help.contents(), QStringLiteral("slug")).mid(6) ==
          QStringList{setupSlug, faqSlug, QStringLiteral("ext/bob.tools/intro")});
    CHECK(help.article(faqSlug).value(QStringLiteral("next")).toMap().isEmpty());
    CHECK(listed(help.contents(), QStringLiteral("label")) ==
          QStringList{QStringLiteral("Guides"), QStringLiteral("Script API"), QStringLiteral("Extensions")});
    CHECK(listed(help.contents(), QStringLiteral("iconPath")).contains(QStringLiteral("M2 2 L22 22")));

    const QVariantMap setup = help.article(setupSlug);
    CHECK(setup.value(QStringLiteral("title")).toString() == QStringLiteral("Setting up"));
    CHECK(setup.value(QStringLiteral("group")).toString() == QStringLiteral("Extensions"));
    CHECK(setup.value(QStringLiteral("baseUrl")).toUrl() == folder);
    CHECK(setup.value(QStringLiteral("text")).toString().startsWith(QStringLiteral("Plug the board")));
    CHECK(setup.value(QStringLiteral("meta")).toList().value(0).toMap().value(QStringLiteral("text")).toString() ==
          QStringLiteral("Alice boards"));
    QStringList pictures;
    for (const QVariant& block : setup.value(QStringLiteral("blocks")).toList()) {
        if (block.toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("image")) {
            pictures.append(block.toMap().value(QStringLiteral("source")).toString());
        }
    }
    CHECK(pictures == QStringList{QStringLiteral("images/board.png")});
    CHECK(setup.value(QStringLiteral("previous")).toMap().isEmpty());
    CHECK(setup.value(QStringLiteral("next")).toMap().value(QStringLiteral("slug")).toString() == faqSlug);
    CHECK(help.article(faqSlug).value(QStringLiteral("icon")).toString() == QStringLiteral("circle-help"));
    CHECK(help.article(QStringLiteral("api/zebra")).value(QStringLiteral("next")).toMap().isEmpty());
    QStringList guides;
    for (const QVariant& guide : help.home().value(QStringLiteral("guides")).toList()) {
        guides.append(guide.toMap().value(QStringLiteral("slug")).toString());
    }
    CHECK_FALSE(guides.contains(setupSlug));

    help.setQuery(QStringLiteral("blink"));
    CHECK(listed(help.contents(), QStringLiteral("slug")) == QStringList{faqSlug});
    CHECK(listed(help.contents(), QStringLiteral("label")) == QStringList{QStringLiteral("Extensions")});
    help.setQuery({});

    REQUIRE(settings.setValue(kLanguage, kChinese));
    CHECK(listed(help.contents(), QStringLiteral("label")).contains(QStringLiteral("扩展")));
    CHECK(help.article(setupSlug).value(QStringLiteral("title")).toString() == QStringLiteral("设置"));
    CHECK(help.article(setupSlug).value(QStringLiteral("text")).toString() == QStringLiteral("把板子插到手机上。\n"));
    CHECK(help.article(faqSlug).value(QStringLiteral("title")).toString() == QStringLiteral("Questions"));

    help.setExtensionTopics({});
    CHECK_FALSE(help.contains(setupSlug));
    CHECK_FALSE(listed(help.contents(), QStringLiteral("label")).contains(QStringLiteral("扩展")));
}

TEST_CASE("ADR-0112: an extension's help is read in time that grows with its length, whatever brackets, quotes, keys, "
          "braces or spaces it repeats")
{
    const QTemporaryDir data;
    const QTemporaryDir root;
    REQUIRE(data.isValid());
    REQUIRE(root.isValid());
    writeSmallLibrary(root.path());
    AppSettings settings(data.path().toStdWString());
    REQUIRE(settings.setValue(kLanguage, kEnglish));
    HelpLibrary help(settings, root.path());
    constexpr qsizetype kRun = 200'000;
    const QStringList texts{
        QString(kRun, u'[') + QStringLiteral("]x(\n"),
        QString(kRun, u'[') + QStringLiteral("]x [see](help:apples)\n"),
        QStringLiteral("](").repeated(kRun / 2) + u'\n',
        QString(kRun, QChar(0x201C)) + u'\n',
        QStringLiteral("<kbd>").repeated(kRun / 5) + u'\n',
        QStringLiteral("| Keys | Does |\n|---|---|\n| ") + QStringLiteral("<kbd>").repeated(kRun / 5) +
            QStringLiteral(" | x |\n"),
        QStringLiteral("## a") + QString(kRun, u' ') + QStringLiteral("b\n"),
        QStringLiteral("## a ") + QStringLiteral("{}").repeated(kRun / 2) + u'\n',
        QStringLiteral("```") + QString(kRun, u' ') + QStringLiteral("!\n"),
        QStringLiteral("| a |\n|---") + QString(kRun, u' ') + QStringLiteral("x\n")};
    std::vector<gs::app::ExtensionHelp> topics;
    for (qsizetype index = 0; index < texts.size(); ++index) {
        topics.push_back({.extension = QStringLiteral("mallory.slow"),
                          .extensionName = QStringLiteral("Slow"),
                          .id = QStringLiteral("t%1").arg(index),
                          .titles = {{kEnglish, QStringLiteral("Slow %1").arg(index)}},
                          .texts = {{kEnglish, texts[index]}}});
    }
    QElapsedTimer timer;
    timer.start();
    help.setExtensionTopics(std::move(topics));
    CHECK(timer.elapsed() < 20'000);
    CHECK(help.contains(QStringLiteral("ext/mallory.slow/t9")));

    const QVariantList blocks =
        help.article(QStringLiteral("ext/mallory.slow/t1")).value(QStringLiteral("blocks")).toList();
    REQUIRE_FALSE(blocks.isEmpty());
    const QVariantMap link = blocks.front().toMap();
    CHECK(link.value(QStringLiteral("kind")).toString() == QStringLiteral("link"));
    CHECK(link.value(QStringLiteral("label")).toString() == QStringLiteral("see"));
    CHECK(link.value(QStringLiteral("target")).toString() == QStringLiteral("help:apples"));
    const QVariantList sections =
        help.article(QStringLiteral("ext/mallory.slow/t7")).value(QStringLiteral("sections")).toList();
    REQUIRE(sections.size() == 1);
    CHECK(sections.front().toMap().value(QStringLiteral("title")).toString() == QStringLiteral("a"));
}
