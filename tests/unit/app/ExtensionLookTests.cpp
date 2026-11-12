#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QFile>
#include <QJSEngine>
#include <QJSValue>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <array>
#include <memory>

namespace {

class Look
{
public:
    Look()
    {
        if (QCoreApplication::instance() == nullptr) {
            m_app = std::make_unique<QCoreApplication>(m_argc, m_argv.data());
        }
        m_engine = std::make_unique<QJSEngine>();
        QFile file(QStringLiteral(GS_EXTENSION_LOOK_FILE));
        REQUIRE(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString source =
            QString::fromUtf8(file.readAll()).replace(QStringLiteral(".pragma library"), QString());
        const QJSValue loaded = m_engine->evaluate(source, QStringLiteral("ExtensionLook.js"));
        REQUIRE_FALSE(loaded.isError());
    }

    [[nodiscard]] QStringList capabilities(const QVariantMap& entry)
    {
        return call("capabilities", {m_engine->toScriptValue(entry)}).toVariant().toStringList();
    }

    [[nodiscard]] QString lead(const QVariantMap& entry)
    {
        return call("lead", {m_engine->toScriptValue(entry)}).toString();
    }

    [[nodiscard]] bool showsSettings(const QVariantMap& entry, int grants)
    {
        return call("showsSettings", {m_engine->toScriptValue(entry), QJSValue(grants)}).toBool();
    }

    [[nodiscard]] QString icon(const QString& kind) { return call("icon", {QJSValue(kind)}).toString(); }

    [[nodiscard]] QString tone(const QString& kind) { return call("tone", {QJSValue(kind)}).toString(); }

private:
    QJSValue call(const char* name, const QJSValueList& arguments)
    {
        const QJSValue result =
            m_engine->globalObject().property(QString::fromLatin1(name)).call(arguments);
        REQUIRE_FALSE(result.isError());
        return result;
    }

    int m_argc = 1;
    char m_name[5] = "test";
    std::array<char*, 2> m_argv{m_name, nullptr};
    std::unique_ptr<QCoreApplication> m_app;
    std::unique_ptr<QJSEngine> m_engine;
};

QVariantMap nativeEntry(const QString& id, const QVariantMap& native)
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("points"), QVariantList{QStringLiteral("native")}},
            {QStringLiteral("permissions"), QVariantList{}},
            {QStringLiteral("native"), native}};
}

}

TEST_CASE("ADR-0107: an extension's capabilities say what it adds, and a native part never shows as Native")
{
    Look look;

    const QVariantMap board = nativeEntry(
        QStringLiteral("gamescrcpy.esp32"),
        {{QStringLiteral("touchMethods"), QVariantList{QStringLiteral("ESP32")}},
         {QStringLiteral("nodes"), QVariantList{}}});
    CHECK(look.capabilities(board) == QStringList{QStringLiteral("hardware")});
    CHECK(look.lead(board) == QStringLiteral("hardware"));
    CHECK(look.icon(QStringLiteral("hardware")) == QStringLiteral("usb"));
    CHECK(look.tone(QStringLiteral("hardware")) == QStringLiteral("amber"));

    const QVariantMap clock = nativeEntry(QStringLiteral("gamescrcpy.pc-clock"),
                                          {{QStringLiteral("nodes"), QVariantList{QStringLiteral("now")}}});
    CHECK(look.capabilities(clock) == QStringList{QStringLiteral("nodes")});

    const QVariantMap feel = nativeEntry(
        QStringLiteral("community.feel-samples"),
        {{QStringLiteral("attachments"), QVariantList{QStringLiteral("Random landing")}},
         {QStringLiteral("behaviors"), QVariantList{QStringLiteral("Circles")}}});
    CHECK(look.capabilities(feel) == QStringList{QStringLiteral("component")});

    const QVariantMap lights{
        {QStringLiteral("id"), QStringLiteral("light.keyboard")},
        {QStringLiteral("points"), QVariantList{QStringLiteral("script"), QStringLiteral("panel")}},
        {QStringLiteral("permissions"),
         QVariantList{QVariantMap{{QStringLiteral("name"), QStringLiteral("hardware")}},
                      QVariantMap{{QStringLiteral("name"), QStringLiteral("notify")}}}}};
    CHECK(look.capabilities(lights) ==
          QStringList{QStringLiteral("script"), QStringLiteral("panel"), QStringLiteral("hardware")});
    CHECK(look.lead(lights) == QStringLiteral("script"));

    const QVariantMap scanlines{{QStringLiteral("id"), QStringLiteral("gamescrcpy.crt-scanlines")},
                                {QStringLiteral("points"), QVariantList{QStringLiteral("filter")}}};
    CHECK(look.capabilities(scanlines) == QStringList{QStringLiteral("filter")});
    CHECK(look.icon(look.lead(scanlines)) == QStringLiteral("monitor-play"));

    const QVariantMap unread{{QStringLiteral("id"), QStringLiteral("broken")}};
    CHECK(look.capabilities(unread).isEmpty());
    CHECK(look.lead(unread).isEmpty());
    CHECK(look.icon(look.lead(unread)) == QStringLiteral("puzzle"));

    for (const QVariantMap& entry : {board, clock, feel, lights, scanlines}) {
        CHECK_FALSE(look.capabilities(entry).contains(QStringLiteral("native")));
    }
}

TEST_CASE("ADR-0112: an extension whose DLL feeds input devices into the key mapping shows as hardware")
{
    Look look;
    const QVariantMap pedal = nativeEntry(
        QStringLiteral("alice.pedal"),
        {{QStringLiteral("inputSources"), QVariantList{QStringLiteral("Foot pedal")}},
         {QStringLiteral("nodes"), QVariantList{}}});
    CHECK(look.capabilities(pedal) == QStringList{QStringLiteral("hardware")});
    CHECK(look.lead(pedal) == QStringLiteral("hardware"));
}

TEST_CASE("ADR-0112: a language pack shows as a language, with the languages icon")
{
    Look look;
    const QVariantMap japanese{{QStringLiteral("id"), QStringLiteral("alice.japanese")},
                               {QStringLiteral("points"), QVariantList{QStringLiteral("locale")}}};
    CHECK(look.capabilities(japanese) == QStringList{QStringLiteral("locale")});
    CHECK(look.lead(japanese) == QStringLiteral("locale"));
    CHECK(look.icon(QStringLiteral("locale")) == QStringLiteral("languages"));
    CHECK(look.tone(QStringLiteral("locale")) == QStringLiteral("azure"));
}

TEST_CASE("ADR-0107: an extension card offers Settings only when the extension is loaded and has parameters, "
          "devices it may use or is the ESP32 touch box")
{
    Look look;

    QVariantMap scanlines{{QStringLiteral("id"), QStringLiteral("gamescrcpy.crt-scanlines")},
                          {QStringLiteral("active"), true},
                          {QStringLiteral("hasSettings"), true}};
    CHECK(look.showsSettings(scanlines, 0));
    scanlines.insert(QStringLiteral("active"), false);
    CHECK_FALSE(look.showsSettings(scanlines, 0));
    CHECK_FALSE(look.showsSettings(scanlines, 2));

    const QVariantMap slate{{QStringLiteral("id"), QStringLiteral("gamescrcpy.slate-theme")},
                            {QStringLiteral("active"), true},
                            {QStringLiteral("hasSettings"), false}};
    CHECK_FALSE(look.showsSettings(slate, 0));
    CHECK(look.showsSettings(slate, 1));

    QVariantMap board{{QStringLiteral("id"), QStringLiteral("gamescrcpy.esp32")},
                      {QStringLiteral("active"), true},
                      {QStringLiteral("hasSettings"), false}};
    CHECK(look.showsSettings(board, 0));
    board.insert(QStringLiteral("active"), false);
    CHECK_FALSE(look.showsSettings(board, 0));
}
