#include "AppSettings.h"
#include "ExtensionHelp.h"
#include "ExtensionHost.h"
#include "HelpLibrary.h"
#include "LanguagePacks.h"
#include "Settings.h"

#include "unit/ext/TestFiles.h"

#include <gs/ext/Manifest.h>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <QByteArray>
#include <QChar>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using gs::app::AppSettings;
using gs::app::ExtensionHost;
using gs::app::HelpLibrary;
using gs::app::LanguagePacks;
using namespace gs::ext::test;
namespace fs = std::filesystem;

namespace {

const QString kLanguage = QStringLiteral("general.language");
const QString kJapanese = QStringLiteral("ja");

class Application
{
public:
    Application()
    {
        if (QCoreApplication::instance() == nullptr) {
            m_app = std::make_unique<QCoreApplication>(m_argc, m_argv.data());
        }
    }

private:
    int m_argc = 1;
    char m_name[5] = "test";
    std::array<char*, 2> m_argv{m_name, nullptr};
    std::unique_ptr<QCoreApplication> m_app;
};

struct Message {
    std::string context;
    std::string source;
    QString translation;
};

std::uint32_t elfHash(std::string_view text)
{
    std::uint32_t hash = 0;
    for (const char c : text) {
        hash = (hash << 4) + static_cast<unsigned char>(c);
        const std::uint32_t high = hash & 0xF0000000U;
        if (high != 0) {
            hash ^= high >> 24;
        }
        hash &= ~high;
    }
    return hash == 0 ? 1 : hash;
}

void putWord(QByteArray& out, std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.append(static_cast<char>((value >> shift) & 0xFFU));
    }
}

void putText(QByteArray& out, char tag, std::string_view text)
{
    out.append(tag);
    putWord(out, static_cast<std::uint32_t>(text.size()));
    out.append(text.data(), static_cast<qsizetype>(text.size()));
}

QByteArray compiledTranslations(const std::vector<Message>& messages)
{
    constexpr char kEndTag = 1;
    constexpr char kTranslationTag = 3;
    constexpr char kSourceTag = 6;
    constexpr char kContextTag = 7;
    constexpr char kHashesSection = 0x42;
    constexpr char kMessagesSection = 0x69;
    constexpr std::array<unsigned char, 16> kMagic{0x3C, 0xB8, 0x64, 0x18, 0xCA, 0xEF, 0x9C, 0x95,
                                                   0xCD, 0x21, 0x1C, 0xBF, 0x60, 0xA1, 0xBD, 0xDD};
    QByteArray records;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> hashes;
    for (const Message& message : messages) {
        hashes.emplace_back(elfHash(message.source), static_cast<std::uint32_t>(records.size()));
        records.append(kTranslationTag);
        putWord(records, static_cast<std::uint32_t>(message.translation.size() * 2));
        for (const QChar unit : message.translation) {
            records.append(static_cast<char>(unit.unicode() >> 8));
            records.append(static_cast<char>(unit.unicode() & 0xFF));
        }
        putText(records, kSourceTag, message.source);
        putText(records, kContextTag, message.context);
        records.append(kEndTag);
    }
    std::ranges::sort(hashes);
    QByteArray table;
    for (const auto& [hash, offset] : hashes) {
        putWord(table, hash);
        putWord(table, offset);
    }
    QByteArray file(reinterpret_cast<const char*>(kMagic.data()), static_cast<qsizetype>(kMagic.size()));
    file.append(kHashesSection);
    putWord(file, static_cast<std::uint32_t>(table.size()));
    file.append(table);
    file.append(kMessagesSection);
    putWord(file, static_cast<std::uint32_t>(records.size()));
    file.append(records);
    return file;
}

const std::vector<Message>& japaneseMessages()
{
    static const std::vector<Message> messages{
        {"Settings", "Interface language", QStringLiteral("表示言語")},
        {"HelpLibrary", "Extensions", QStringLiteral("拡張機能")},
        {"ExtensionsPage", "Languages", QStringLiteral("言語")},
    };
    return messages;
}

void writeBytes(const fs::path& path, const QByteArray& bytes)
{
    writeFile(path, std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
}

void writeText(const QString& path, const QString& contents)
{
    REQUIRE(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray bytes = contents.toUtf8();
    REQUIRE(file.write(bytes) == bytes.size());
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

struct Fixture {
    Scratch scratch;
    fs::path data = scratch / "data";
    fs::path user = scratch / "user";

    void addPack(std::string_view id, std::string_view locale, std::string_view name,
                 const std::vector<Message>& messages) const
    {
        const fs::path folder = user / pathOf(id) / "1.0.0";
        const nlohmann::json manifest = {
            {"schemaVersion", 1},
            {"id", std::string(id)},
            {"version", "1.0.0"},
            {"name", "Japanese words"},
            {"license", "MIT"},
            {"requires", {{"locale", "^1.0"}}},
            {"contributes",
             {{"locales", nlohmann::json::array({{{"locale", std::string(locale)},
                                                  {"name", std::string(name)},
                                                  {"translations", "i18n/pack.qm"},
                                                  {"help", "help"}}})}}},
            {"l10n", {{"ja", {{"name", "日本語の言葉"}}}}}};
        writeFile(folder / "manifest.json", manifest.dump());
        writeBytes(folder / "i18n/pack.qm", compiledTranslations(messages));
        writeFile(folder / "help/apples.md", "# りんご\n\n赤いりんごは木になります。\n");
    }
};

}

TEST_CASE("ADR-0112: the language setting keeps system, zh_CN and en, takes a pack's language only while a pack "
          "offers it, and follows Windows while the pack is gone")
{
    using gs::app::settings::Settings;
    CHECK(gs::app::settings::languageTag("ja"));
    CHECK(gs::app::settings::languageTag("pt_BR"));
    CHECK(gs::app::settings::languageTag("yue"));
    CHECK_FALSE(gs::app::settings::languageTag("pt-BR"));
    CHECK_FALSE(gs::app::settings::languageTag("zh_Hant"));
    CHECK_FALSE(gs::app::settings::languageTag("j"));
    CHECK_FALSE(gs::app::settings::languageTag("JA"));
    CHECK_FALSE(gs::app::settings::languageTag("system "));

    const auto old = gs::app::settings::load<Settings>(
        R"({"format": "gamescrcpy.settings", "version": 1, "general": {"language": "en"}})");
    CHECK(old.diagnostics.empty());
    CHECK(old.document.general.language == "en");
    const auto pack = gs::app::settings::load<Settings>(
        R"({"format": "gamescrcpy.settings", "version": 1, "general": {"language": "zh_TW"}})");
    CHECK(pack.diagnostics.empty());
    CHECK(pack.document.general.language == "zh_TW");
    CHECK(gs::app::settings::save(pack.document).find("\"zh_TW\"") != std::string::npos);
    const auto wrong = gs::app::settings::load<Settings>(
        R"({"format": "gamescrcpy.settings", "version": 1, "general": {"language": "Klingon"}})");
    CHECK_FALSE(wrong.diagnostics.empty());
    CHECK(wrong.document.general.language == "system");

    Scratch scratch;
    QString system;
    {
        AppSettings settings(scratch / "data");
        int changes = 0;
        QObject::connect(&settings, &AppSettings::languageChanged, &settings, [&changes] { ++changes; });
        system = settings.interfaceLanguage();
        CHECK((system == QStringLiteral("en") || system == QStringLiteral("zh_CN")));

        CHECK_FALSE(settings.setValue(kLanguage, kJapanese));
        CHECK_FALSE(settings.setValue(kLanguage, QStringLiteral("Japanese")));
        CHECK(settings.language() == QStringLiteral("system"));
        settings.setLanguagePacks({kJapanese});
        CHECK(changes == 0);
        CHECK(settings.interfaceLanguage() == system);
        REQUIRE(settings.setValue(kLanguage, kJapanese));
        CHECK(changes == 1);
        CHECK(settings.language() == kJapanese);
        CHECK(settings.interfaceLanguage() == kJapanese);
        CHECK(settings.values().value(kLanguage).toString() == kJapanese);

        settings.setLanguagePacks({});
        CHECK(changes == 2);
        CHECK(settings.language() == kJapanese);
        CHECK(settings.interfaceLanguage() == system);
        CHECK(settings.setValue(QStringLiteral("video.maxFps"), 30));
        CHECK(settings.language() == kJapanese);
        CHECK_FALSE(settings.setValue(kLanguage, QStringLiteral("ko")));

        settings.setLanguagePacks({QStringLiteral("ko"), kJapanese});
        CHECK(changes == 3);
        CHECK(settings.interfaceLanguage() == kJapanese);
        settings.setLanguagePacks({kJapanese, QStringLiteral("ko")});
        CHECK(changes == 3);
        REQUIRE(settings.setValue(kLanguage, QStringLiteral("zh_CN")));
        CHECK(settings.interfaceLanguage() == QStringLiteral("zh_CN"));
        REQUIRE(settings.setValue(kLanguage, kJapanese));
        settings.flush();
    }
    const AppSettings reloaded(scratch / "data");
    CHECK(reloaded.language() == kJapanese);
    CHECK(reloaded.interfaceLanguage() == system);
}

TEST_CASE("ADR-0112: an enabled language pack is listed with its own name and its extension's name, choosing it "
          "loads its translations over English, and turning it off or removing it falls back and says so")
{
    const Application application;
    const Fixture fixture;
    fixture.addPack("alice.japanese", "ja", "日本語", japaneseMessages());
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);
    LanguagePacks languages(settings);
    int translations = 0;
    QObject::connect(&languages, &LanguagePacks::translationsChanged, &languages,
                     [&translations] { ++translations; });
    languages.follow(host);
    CHECK(languages.packList().isEmpty());
    CHECK(translations == 0);

    REQUIRE(host.enable(QStringLiteral("alice.japanese"), true));
    REQUIRE(languages.packList().size() == 1);
    const QVariantMap pack = languages.packList().front().toMap();
    CHECK(pack.value(QStringLiteral("locale")).toString() == kJapanese);
    CHECK(pack.value(QStringLiteral("name")).toString() == QStringLiteral("日本語"));
    CHECK(pack.value(QStringLiteral("extension")).toString() == QStringLiteral("alice.japanese"));
    CHECK(pack.value(QStringLiteral("extensionName")).toString() == QStringLiteral("Japanese words"));
    CHECK(settings.languagePacks() == QStringList{kJapanese});
    CHECK(languages.loaded().isEmpty());
    CHECK(languages.missing().isEmpty());
    CHECK(translations == 0);

    REQUIRE(settings.setValue(kLanguage, kJapanese));
    CHECK(settings.interfaceLanguage() == kJapanese);
    CHECK(languages.loaded() == kJapanese);
    CHECK(translations == 1);
    CHECK(QCoreApplication::translate("Settings", "Interface language") == QStringLiteral("表示言語"));
    CHECK(QCoreApplication::translate("ExtensionsPage", "Languages") == QStringLiteral("言語"));
    CHECK(QCoreApplication::translate("Settings", "Theme") == QStringLiteral("Theme"));
    CHECK(languages.packList().front().toMap().value(QStringLiteral("extensionName")).toString() ==
          QStringLiteral("日本語の言葉"));
    QVariantMap entry;
    for (const QVariant& each : host.entries()) {
        if (each.toMap().value(QStringLiteral("id")).toString() == QStringLiteral("alice.japanese")) {
            entry = each.toMap();
        }
    }
    CHECK(entry.value(QStringLiteral("name")).toString() == QStringLiteral("日本語の言葉"));
    CHECK(entry.value(QStringLiteral("points")).toStringList() == QStringList{QStringLiteral("locale")});
    REQUIRE(entry.value(QStringLiteral("locales")).toList().size() == 1);
    CHECK(entry.value(QStringLiteral("locales")).toList().front().toMap().value(QStringLiteral("name")).toString() ==
          QStringLiteral("日本語"));

    REQUIRE(host.enable(QStringLiteral("alice.japanese"), false));
    CHECK(languages.packList().isEmpty());
    CHECK(settings.language() == kJapanese);
    CHECK(settings.interfaceLanguage() != kJapanese);
    CHECK(languages.loaded().isEmpty());
    CHECK(translations == 2);
    CHECK(QCoreApplication::translate("Settings", "Interface language") != QStringLiteral("表示言語"));
    QVariantMap missing = languages.missing();
    CHECK(missing.value(QStringLiteral("locale")).toString() == kJapanese);
    CHECK(missing.value(QStringLiteral("name")).toString() == QStringLiteral("日本語"));
    CHECK(missing.value(QStringLiteral("extensionName")).toString() == QStringLiteral("Japanese words"));
    CHECK(missing.value(QStringLiteral("installed")).toBool());

    REQUIRE(host.enable(QStringLiteral("alice.japanese"), true));
    CHECK(settings.interfaceLanguage() == kJapanese);
    CHECK(languages.loaded() == kJapanese);
    CHECK(languages.missing().isEmpty());
    CHECK(QCoreApplication::translate("Settings", "Interface language") == QStringLiteral("表示言語"));

    REQUIRE(host.remove(QStringLiteral("alice.japanese"), QStringLiteral("1.0.0")));
    CHECK_FALSE(fs::exists(fixture.user / "alice.japanese"));
    CHECK(languages.packList().isEmpty());
    CHECK(languages.loaded().isEmpty());
    CHECK(settings.language() == kJapanese);
    missing = languages.missing();
    CHECK(missing.value(QStringLiteral("name")).toString() == kJapanese);
    CHECK_FALSE(missing.value(QStringLiteral("installed")).toBool());

    REQUIRE(settings.setValue(kLanguage, QStringLiteral("en")));
    CHECK(languages.missing().isEmpty());
    CHECK(QCoreApplication::translate("Settings", "Interface language") == QStringLiteral("Interface language"));
}

TEST_CASE("ADR-0112: when two extensions add the same language the first one is used, and a pack that cannot be "
          "read leaves the interface in English")
{
    const Application application;
    const Fixture fixture;
    fixture.addPack("alice.japanese", "ja", "日本語", japaneseMessages());
    fixture.addPack("bob.japanese", "ja", "日本語 Bob", japaneseMessages());
    fixture.addPack("carol.korean", "ko", "한국어", {{"Settings", "Interface language", QStringLiteral("인터페이스 언어")}});
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);
    for (const QString& id : {QStringLiteral("alice.japanese"), QStringLiteral("bob.japanese"),
                              QStringLiteral("carol.korean")}) {
        REQUIRE(host.enable(id, true));
    }
    const std::vector<gs::app::LanguagePack> packs = gs::app::extensionLanguagePacks(host, QStringLiteral("en"));
    REQUIRE(packs.size() == 2);
    const auto japanese = std::ranges::find(packs, kJapanese, &gs::app::LanguagePack::locale);
    REQUIRE(japanese != packs.end());
    CHECK(japanese->extension == QStringLiteral("alice.japanese"));
    CHECK(japanese->translations.endsWith(QStringLiteral("pack.qm")));
    CHECK(japanese->help.endsWith(QStringLiteral("help")));
    CHECK(std::ranges::find(packs, QStringLiteral("ko"), &gs::app::LanguagePack::locale) != packs.end());

    LanguagePacks languages(settings);
    languages.follow(host);
    REQUIRE(settings.setValue(kLanguage, QStringLiteral("ko")));
    CHECK(languages.loaded() == QStringLiteral("ko"));
    CHECK(QCoreApplication::translate("Settings", "Interface language") == QStringLiteral("인터페이스 언어"));

    const fs::path korean = fixture.user / "carol.korean/1.0.0/i18n/pack.qm";
    writeFile(korean, "not a translation");
    std::vector<gs::app::LanguagePack> broken = languages.packs();
    for (gs::app::LanguagePack& each : broken) {
        each.translations += QStringLiteral(".gone");
    }
    languages.setPacks(broken);
    CHECK(settings.interfaceLanguage() == QStringLiteral("ko"));
    CHECK(languages.loaded().isEmpty());
    CHECK(QCoreApplication::translate("Settings", "Interface language") == QStringLiteral("Interface language"));
    QByteArray data;
    QTranslator translator;
    CHECK_FALSE(gs::app::loadTranslationFile(translator, data, QString::fromStdWString(korean.wstring())));
    CHECK(data.isEmpty());
}

TEST_CASE("ADR-0112: extension texts, extension help and GameScrcpy's own help use the pack's language, with the base "
          "language and then English as fallbacks")
{
    const Application application;
    gs::ext::Manifest manifest;
    manifest.l10n["zh"] = {{"name", "中文名字"}};
    manifest.l10n["ja"] = {{"name", "日本語の名前"}};
    CHECK(gs::app::extensionText(manifest, "name", "Plain", QStringLiteral("zh_TW")) == QStringLiteral("中文名字"));
    CHECK(gs::app::extensionText(manifest, "name", "Plain", kJapanese) == QStringLiteral("日本語の名前"));
    CHECK(gs::app::extensionText(manifest, "name", "Plain", QStringLiteral("ko")) == QStringLiteral("Plain"));

    const Fixture fixture;
    const fs::path helpful = fixture.user / "alice.helpful/1.0.0";
    writeFile(helpful / "manifest.json", R"({"schemaVersion": 1, "id": "alice.helpful", "version": "1.0.0",
        "name": "Helpful", "license": "MIT", "requires": {"help": "^1.0"},
        "contributes": {"help": [{"id": "setup", "title": "Setting up", "file": "help/setup.md",
                                  "locales": {"ja": "help/setup.ja.md"}}]},
        "l10n": {"ja": {"name": "役に立つ", "help.setup.title": "準備"}}})");
    writeFile(helpful / "help/setup.md", "Plug it in.\n");
    writeFile(helpful / "help/setup.ja.md", "差し込みます。\n");
    AppSettings settings(fixture.data);
    settings.setLanguagePacks({kJapanese, QStringLiteral("zh_TW")});
    REQUIRE(settings.setValue(kLanguage, kJapanese));
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);
    REQUIRE(host.enable(QStringLiteral("alice.helpful"), true));
    const std::vector<gs::app::ExtensionHelp> topics = gs::app::extensionHelp(host, kJapanese);
    REQUIRE(topics.size() == 1);
    CHECK(topics[0].extensionName == QStringLiteral("役に立つ"));
    CHECK(topics[0].titles.at(kJapanese) == QStringLiteral("準備"));
    CHECK(topics[0].titles.at(QStringLiteral("en")) == QStringLiteral("Setting up"));
    CHECK(topics[0].texts.at(kJapanese) == QStringLiteral("差し込みます。\n"));
    for (const ExtensionHost::Extension& extension : host.extensions()) {
        if (extension.manifest && extension.manifest->id == "alice.helpful") {
            CHECK(host.textOf(*extension.manifest, "help.setup.title", "Setting up") == QStringLiteral("準備"));
        }
    }

    const QTemporaryDir root;
    REQUIRE(root.isValid());
    writeText(root.path() + QStringLiteral("/index.json"), QStringLiteral(R"({"groups": [
        {"title": {"en": "Guides", "zh_CN": "指南"}, "articles": [
            {"slug": "apples", "icon": "smartphone", "title": {"en": "Apples", "zh_CN": "苹果"}},
            {"slug": "pears", "icon": "keyboard", "title": {"en": "Pears", "zh_CN": "梨"}}]}]})"));
    writeText(root.path() + QStringLiteral("/en/apples.md"), QStringLiteral("Red apples grow on trees.\n"));
    writeText(root.path() + QStringLiteral("/zh_CN/apples.md"), QStringLiteral("红苹果长在树上。\n"));
    writeText(root.path() + QStringLiteral("/en/pears.md"), QStringLiteral("Green pears are sweet.\n"));
    const QString packFolder = root.path() + QStringLiteral("/pack");
    writeText(packFolder + QStringLiteral("/help/apples.md"),
              QStringLiteral("# りんご\n\n赤いりんごは木になります。\n"));
    const QString qm = packFolder + QStringLiteral("/ja.qm");
    writeBytes(fs::path(qm.toStdWString()), compiledTranslations(japaneseMessages()));

    HelpLibrary help(settings, root.path());
    CHECK(help.language() == kJapanese);
    CHECK(help.article(QStringLiteral("apples")).value(QStringLiteral("title")).toString() == QStringLiteral("Apples"));
    int contents = 0;
    QObject::connect(&help, &HelpLibrary::contentsChanged, &help, [&contents] { ++contents; });
    help.setLanguagePacks({{.code = kJapanese, .translations = qm, .folder = packFolder + QStringLiteral("/help")}});
    CHECK(contents == 1);
    const QVariantMap apples = help.article(QStringLiteral("apples"));
    CHECK(apples.value(QStringLiteral("title")).toString() == QStringLiteral("りんご"));
    CHECK(apples.value(QStringLiteral("text")).toString() == QStringLiteral("赤いりんごは木になります。\n"));
    CHECK(apples.value(QStringLiteral("group")).toString() == QStringLiteral("Guides"));
    const QVariantMap pears = help.article(QStringLiteral("pears"));
    CHECK(pears.value(QStringLiteral("title")).toString() == QStringLiteral("Pears"));
    CHECK(pears.value(QStringLiteral("text")).toString() == QStringLiteral("Green pears are sweet.\n"));
    CHECK(help.article(QStringLiteral("apples"), QStringLiteral("zh_CN")).value(QStringLiteral("title")).toString() ==
          QStringLiteral("苹果"));
    help.setQuery(QStringLiteral("木"));
    CHECK(listed(help.contents(), QStringLiteral("slug")) == QStringList{QStringLiteral("apples")});
    help.setQuery({});
    const int before = contents;
    help.setLanguagePacks({{.code = kJapanese, .translations = qm, .folder = packFolder + QStringLiteral("/help")}});
    CHECK(contents == before);

    help.setExtensionTopics(gs::app::extensionHelp(host, kJapanese));
    CHECK(listed(help.contents(), QStringLiteral("label")).contains(QStringLiteral("拡張機能")));
    CHECK(help.article(QStringLiteral("ext/alice.helpful/setup")).value(QStringLiteral("title")).toString() ==
          QStringLiteral("準備"));
    CHECK(help.article(QStringLiteral("ext/alice.helpful/setup")).value(QStringLiteral("text")).toString() ==
          QStringLiteral("差し込みます。\n"));
    help.setQuery(QStringLiteral("差し込み"));
    CHECK(listed(help.contents(), QStringLiteral("slug")) == QStringList{QStringLiteral("ext/alice.helpful/setup")});
    help.setQuery({});
    help.setExtensionTopics({{.extension = QStringLiteral("bob.tools"),
                              .extensionName = QStringLiteral("Bob tools"),
                              .id = QStringLiteral("intro"),
                              .titles = {{QStringLiteral("en"), QStringLiteral("Intro")}},
                              .texts = {{QStringLiteral("en"), QStringLiteral("Hello.\n")},
                                        {QStringLiteral("zh"), QStringLiteral("你好。\n")}}}});
    CHECK(help.article(QStringLiteral("ext/bob.tools/intro"), QStringLiteral("zh_TW"))
              .value(QStringLiteral("text"))
              .toString() == QStringLiteral("你好。\n"));
    CHECK(help.article(QStringLiteral("ext/bob.tools/intro")).value(QStringLiteral("text")).toString() ==
          QStringLiteral("Hello.\n"));

    help.setLanguagePacks({});
    CHECK(help.article(QStringLiteral("apples")).value(QStringLiteral("title")).toString() == QStringLiteral("Apples"));
    CHECK(help.article(QStringLiteral("apples")).value(QStringLiteral("text")).toString() ==
          QStringLiteral("Red apples grow on trees.\n"));
    CHECK_FALSE(listed(help.contents(), QStringLiteral("label")).contains(QStringLiteral("拡張機能")));

    HelpLibrary reference(settings, QStringLiteral(":/help"), [](const QString& language, const QString& text) {
        return language == QStringLiteral("ja") ? QStringLiteral("訳 ") + text : QString();
    });
    reference.setLanguagePacks({{.code = kJapanese, .translations = {}, .folder = {}}});
    CHECK(reference.article(QStringLiteral("api/touch"))
              .value(QStringLiteral("lead"))
              .toString()
              .startsWith(QStringLiteral("訳 Touches on the phone picture.")));
    CHECK(reference.article(QStringLiteral("api/touch"), QStringLiteral("en"))
              .value(QStringLiteral("lead"))
              .toString()
              .startsWith(QStringLiteral("Touches on the phone picture.")));
}
