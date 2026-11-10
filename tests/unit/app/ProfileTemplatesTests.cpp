#include "ProfileEditor.h"
#include "ProfileLibrary.h"

#include <gs/input/Geometry.h>
#include <gs/input/Profile.h>

#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTranslator>

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

using gs::app::ProfileEditor;
using gs::app::ProfileLibrary;
using namespace gs::input;

namespace {

constexpr const char* kShooter = R"({"format":"gamescrcpy.profile","version":3,
    "meta":{"name":"Shooter","app":"com.example.shooter"},
    "controls":[{"id":"fire","type":"hold","inputs":{"press":"Mouse.Left"}}]})";

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

class TemplateTranslator final : public QTranslator
{
public:
    TemplateTranslator() { QCoreApplication::installTranslator(this); }
    ~TemplateTranslator() override { QCoreApplication::removeTranslator(this); }

    [[nodiscard]] bool isEmpty() const override { return false; }

    [[nodiscard]] QString translate(const char* context, const char* sourceText, const char*, int) const override
    {
        if (std::string_view(context) != "ProfileTemplates") {
            return {};
        }
        return QStringLiteral("T:") + QString::fromUtf8(sourceText);
    }
};

void write(const QString& path, const char* text)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(text);
}

ProfileLibrary::Discard deleting()
{
    return [](const QString& path) { return QFile::remove(path); };
}

LoadResult templateFile(const QString& id)
{
    QFile file(QStringLiteral(":/templates/%1.json").arg(id));
    REQUIRE(file.open(QIODevice::ReadOnly));
    return loadProfile(file.readAll().toStdString());
}

Vec2 placeOf(const Profile& profile, std::string_view id)
{
    const auto found = std::ranges::find(profile.controls, id, &Control::id);
    REQUIRE(found != profile.controls.end());
    const CoordinateMapper map({.width = 16, .height = 9}, ScaleMode::ShortSide, SafeArea::Ignore);
    return map.position(found->anchor, found->offset);
}

}

TEST_CASE("the built-in templates load cleanly, without conflicts, with their keys anchored to their thirds")
{
    const Application app;
    const std::map<QString, int> counts{{QStringLiteral("shooter"), 12}, {QStringLiteral("moba"), 10},
                                        {QStringLiteral("racing"), 6},   {QStringLiteral("action"), 14},
                                        {QStringLiteral("cards"), 4},    {QStringLiteral("blank"), 0}};
    const CoordinateMapper map({.width = 16, .height = 9}, ScaleMode::ShortSide, SafeArea::Ignore);
    for (const auto& [id, count] : counts) {
        INFO(id.toStdString());
        const LoadResult loaded = templateFile(id);
        CHECK(loaded.ok());
        CHECK(std::ranges::none_of(loaded.diagnostics, [](const Diagnostic& d) { return d.severity != Severity::Info; }));
        CHECK(inputConflicts(loaded.profile).empty());
        CHECK(loaded.profile.meta.orientation == Orientation::Landscape);
        CHECK(static_cast<int>(loaded.profile.controls.size()) == count);
        for (const Control& control : loaded.profile.controls) {
            INFO(control.id);
            CHECK_FALSE(control.name.empty());
            const Vec2 at = map.position(control.anchor, control.offset);
            CHECK(at.x > 0.0);
            CHECK(at.x < 1.0);
            CHECK(at.y > 0.0);
            CHECK(at.y < 1.0);
            CHECK(map.anchorAt(at) == control.anchor);
        }
    }

    const Profile shooter = templateFile(QStringLiteral("shooter")).profile;
    CHECK(placeOf(shooter, "move").x == doctest::Approx(0.13).epsilon(0.005));
    CHECK(placeOf(shooter, "fire").x == doctest::Approx(0.82).epsilon(0.005));
    CHECK(placeOf(shooter, "fire").y == doctest::Approx(0.62).epsilon(0.005));
    CHECK(placeOf(shooter, "map").y == doctest::Approx(0.10).epsilon(0.005));
    const Profile moba = templateFile(QStringLiteral("moba")).profile;
    CHECK(placeOf(moba, "skill1").x == doctest::Approx(0.72).epsilon(0.005));
    CHECK(placeOf(moba, "skill1").y == doctest::Approx(0.86).epsilon(0.005));
    CHECK(std::get<SkillControl>(moba.controls[2].kind).direction == AimDirection::Cursor);
    CHECK(std::get<WasdControl>(moba.controls[0].kind).up.has_value());
    const Profile cards = templateFile(QStringLiteral("cards")).profile;
    CHECK(placeOf(cards, "card1").x == doctest::Approx(0.30).epsilon(0.005));
    CHECK(placeOf(cards, "card4").x == doctest::Approx(0.70).epsilon(0.005));
    CHECK(placeOf(cards, "card4").y == doctest::Approx(0.85).epsilon(0.005));
}

TEST_CASE("the template list gives each template its translated name, its keys and a preview")
{
    const Application app;
    const TemplateTranslator translator;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const ProfileLibrary library(dir.path().toStdWString(), deleting());

    const QVariantList templates = library.templates();
    REQUIRE(templates.size() == 6);
    const QVariantMap shooter = templates[0].toMap();
    CHECK(shooter[QStringLiteral("id")].toString() == QStringLiteral("shooter"));
    CHECK(shooter[QStringLiteral("name")].toString() == QStringLiteral("T:Shooter"));
    CHECK(shooter[QStringLiteral("icon")].toString() == QStringLiteral("crosshair"));
    CHECK(shooter[QStringLiteral("controls")].toInt() == 12);
    CHECK(shooter[QStringLiteral("markers")].toList().size() == 12);
    CHECK(shooter[QStringLiteral("portraitMarkers")].toList().size() == 12);
    const QStringList includes = shooter[QStringLiteral("includes")].toStringList();
    REQUIRE(includes.size() == 12);
    CHECK(includes[0] == QStringLiteral("T:Move"));
    CHECK(includes[3] == QStringLiteral("T:Aim down sights"));
    const QVariantMap blank = templates[5].toMap();
    CHECK(blank[QStringLiteral("id")].toString() == QStringLiteral("blank"));
    CHECK(blank[QStringLiteral("controls")].toInt() == 0);
}

TEST_CASE("a profile made from a template is written to the library with its name, game, orientation and key names")
{
    const Application app;
    const TemplateTranslator translator;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    ProfileLibrary library(dir.path().toStdWString(), deleting());
    int entries = 0;
    QObject::connect(&library, &ProfileLibrary::entriesChanged, [&entries] { ++entries; });

    const QString shooter = library.createFromTemplate(QStringLiteral("shooter"), QStringLiteral("  My shooter "),
                                                       QStringLiteral("com.example.shooter"), false);
    CHECK(shooter == QStringLiteral("My shooter"));
    CHECK(entries == 1);
    CHECK(QFile::exists(dir.filePath(QStringLiteral("My shooter.json"))));
    const auto* entry = library.find(shooter);
    REQUIRE(entry != nullptr);
    CHECK(entry->name == QStringLiteral("My shooter"));
    CHECK(entry->app == QStringLiteral("com.example.shooter"));
    CHECK_FALSE(entry->readOnly());
    CHECK(entry->warnings == 0);
    REQUIRE(entry->profile != nullptr);
    CHECK(entry->profile->controls.size() == 12);
    CHECK(entry->profile->controls[0].name == "T:Move");
    CHECK(entry->profile->controls[11].name == "T:Map");
    CHECK(entry->profile->meta.orientation == Orientation::Landscape);
    CHECK(library.linkedTo(QStringLiteral("com.example.shooter")) == entry);

    const QString again = library.createFromTemplate(QStringLiteral("shooter"), QStringLiteral("My shooter"), {}, false);
    CHECK(again == QStringLiteral("My shooter-2"));

    const QString cards = library.createFromTemplate(QStringLiteral("cards"), {}, {}, true);
    CHECK(cards == QStringLiteral("T-Cards - Tapping"));
    REQUIRE(library.profile(cards) != nullptr);
    CHECK(library.profile(cards)->meta.name == "T:Cards / Tapping");
    CHECK(library.profile(cards)->meta.orientation == Orientation::Portrait);
    CHECK(library.find(cards)->portrait);
    CHECK(library.profile(cards)->meta.app.empty());

    const QString blank = library.createFromTemplate(QStringLiteral("blank"), QStringLiteral("con"), {}, false);
    CHECK(blank == QStringLiteral("_con"));
    REQUIRE(library.profile(blank) != nullptr);
    CHECK(library.profile(blank)->controls.empty());

    CHECK(library.createFromTemplate(QStringLiteral("nonsense"), QStringLiteral("x"), {}, false).isEmpty());
    CHECK(library.createFromTemplate(QStringLiteral("blank"), QStringLiteral("x"), QString(256, u'a'), false).isEmpty());
    CHECK(library.all().size() == 4);
    CHECK(QDir(dir.path()).entryList(QDir::Files).size() == 4);
}

TEST_CASE("save as writes the edits as a new profile of the same game and the editor goes on with it saved")
{
    const Application app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    write(dir.filePath(QStringLiteral("shooter.json")), kShooter);
    ProfileLibrary library(dir.path().toStdWString(), deleting());
    CHECK(library.copyName(QStringLiteral("Shooter")) == QStringLiteral("Shooter copy"));
    CHECK(library.copyName(QString(70, u'x')).toUtf8().size() <= 64);

    ProfileEditor editor(*library.profile(QStringLiteral("shooter")), {.width = 2400, .height = 1080});
    editor.addControl(QStringLiteral("hold"), 0.8, 0.8);
    editor.addControl(QStringLiteral("hold"), 0.7, 0.8);
    REQUIRE(editor.dirty());

    Profile copy = editor.profile();
    copy.meta.name = "Shooter night";
    const QString id = library.add(copy);
    CHECK(id == QStringLiteral("Shooter night"));
    editor.markSavedAs(QStringLiteral("Shooter night"));
    CHECK_FALSE(editor.dirty());
    CHECK(editor.profileName() == QStringLiteral("Shooter night"));
    CHECK(editor.canUndo());
    REQUIRE(library.profile(id) != nullptr);
    CHECK(*library.profile(id) == editor.profile());
    CHECK(library.find(id)->app == QStringLiteral("com.example.shooter"));
    CHECK(library.profile(QStringLiteral("shooter"))->controls.size() == 1);

    editor.undo();
    CHECK(editor.dirty());
    CHECK(editor.profileName() == QStringLiteral("Shooter night"));
    CHECK(editor.profile().controls.size() == 2);
    editor.redo();
    CHECK_FALSE(editor.dirty());

    Profile broken = copy;
    broken.meta.name = std::string(65, 'n');
    CHECK(library.add(broken).isEmpty());
}

TEST_CASE("an editor takes back a restored profile as a saved step it can undo")
{
    const Application app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    write(dir.filePath(QStringLiteral("shooter.json")), kShooter);
    const ProfileLibrary library(dir.path().toStdWString(), deleting());
    const Profile original = *library.profile(QStringLiteral("shooter"));

    ProfileEditor editor(original, {.width = 2400, .height = 1080});
    editor.addControl(QStringLiteral("hold"), 0.8, 0.8);
    editor.markSaved();
    editor.addControl(QStringLiteral("hold"), 0.7, 0.8);
    editor.replaceSaved(original);
    CHECK(editor.profile() == original);
    CHECK_FALSE(editor.dirty());
    CHECK(editor.canUndo());
    editor.undo();
    CHECK(editor.profile().controls.size() == 3);
    CHECK(editor.dirty());
}
