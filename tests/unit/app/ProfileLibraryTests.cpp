#include "ComponentLibrary.h"
#include "ProfileLibrary.h"

#include <gs/ext/Archive.h>

#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUrl>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using gs::app::ProfileLibrary;
using Remembered = std::map<std::string, std::string, std::less<>>;

namespace {

constexpr const char* kShooter = R"({"format":"gamescrcpy.profile","version":3,
    "meta":{"name":"Shooter","app":"com.example.shooter"},
    "controls":[{"id":"fire","type":"hold","inputs":{"press":"Mouse.Left"}},
                {"id":"move","type":"wasd","anchor":"bottomLeft","offset":[0.3,-0.3],"inputs":{"up":"KeyW"}}]})";
constexpr const char* kDriving = R"({"format":"gamescrcpy.profile","version":3,
    "meta":{"name":"Driving","app":"com.example.shooter"},"controls":[]})";
constexpr const char* kRacer = R"({"format":"gamescrcpy.profile","version":3,
    "meta":{"name":"Racer","app":"com.example.racer","orientation":"portrait"},"controls":[]})";
constexpr const char* kBroken = R"({"format":"gamescrcpy.profile","version":3,
    "controls":[{"id":"x","type":"nonsense"}]})";
constexpr const char* kGeneral = R"({"format":"gamescrcpy.profile","version":3,
    "meta":{"name":"General"},"controls":[]})";
constexpr const char* kShooterUpdate = R"({"format":"gamescrcpy.profile","version":3,
    "meta":{"name":"Shooter","app":"com.example.shooter"},
    "controls":[{"id":"fire","type":"hold","anchor":"right","offset":[-0.3,0.1],"inputs":{"press":"Mouse.Left"}},
                {"id":"move","type":"wasd","anchor":"bottomLeft","offset":[0.3,-0.3],
                 "inputs":{"up":"KeyW","down":"KeyS"}},
                {"id":"map","type":"tap","anchor":"topRight","offset":[-0.2,0.1],"inputs":{"press":"KeyM"}}]})";
constexpr const char* kBrokenPatch = R"({"format":"gamescrcpy.profile-patch","version":1,
    "controls":{"change":{"fire":{"when":"ghost"}}}})";
constexpr const char* kLogicDemo = R"({"format":"gamescrcpy.profile","version":3,
    "meta":{"name":"Logic demo"},
    "controls":[{"id":"jump","type":"tap","inputs":{"press":"Space"}}]})";

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

void write(const QString& path, const char* text)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(text);
}

ProfileLibrary::Discard deleting(QStringList& discarded)
{
    return [&discarded](const QString& path) {
        discarded.append(path);
        return QFile::remove(path);
    };
}

ProfileLibrary::Discard deletingFolders(QStringList& discarded)
{
    return [&discarded](const QString& path) {
        discarded.append(path);
        return QFileInfo(path).isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
    };
}

void makeDemo(const QDir& root, const QString& name)
{
    REQUIRE(root.mkpath(name + QStringLiteral("/res")));
    REQUIRE(root.mkpath(name + QStringLiteral("/node_modules/pkg")));
    REQUIRE(root.mkpath(name + QStringLiteral("/.git")));
    write(root.filePath(name + QStringLiteral("/profile.json")), kLogicDemo);
    write(root.filePath(name + QStringLiteral("/logic.ts")), "export function go(): void {}\n");
    write(root.filePath(name + QStringLiteral("/res/a.txt")), "picture");
    write(root.filePath(name + QStringLiteral("/node_modules/pkg/index.js")), "module.exports = 1;");
    write(root.filePath(name + QStringLiteral("/.git/config")), "[core]");
}

std::vector<std::string> archived(const QString& package)
{
    const gs::ext::ArchiveListing listing = gs::ext::inspectArchive(package.toStdWString());
    REQUIRE(listing.ok());
    std::vector<std::string> names;
    for (const gs::ext::ArchiveEntry& file : listing.files) {
        names.push_back(file.path);
    }
    std::ranges::sort(names);
    return names;
}

QString leafOf(const std::filesystem::path& path)
{
    return QFileInfo(QString::fromStdWString(path.wstring())).fileName();
}

gs::input::Profile loaded(const char* json)
{
    gs::input::LoadResult result = gs::input::loadProfile(json);
    REQUIRE(result.ok());
    return result.profile;
}

gs::input::Control& control(gs::input::Profile& profile, std::string_view id)
{
    const auto found = std::ranges::find(profile.controls, id, &gs::input::Control::id);
    REQUIRE(found != profile.controls.end());
    return *found;
}

gs::input::Binding& press(gs::input::Profile& profile, std::string_view id)
{
    auto* button = std::get_if<gs::input::HoldControl>(&control(profile, id).kind);
    REQUIRE(button != nullptr);
    REQUIRE(button->press.has_value());
    return *button->press;
}

std::vector<std::string> controlIds(const gs::input::Profile& profile)
{
    std::vector<std::string> ids;
    for (const gs::input::Control& each : profile.controls) {
        ids.push_back(each.id);
    }
    return ids;
}

}

TEST_CASE("the profile library lists the profile files with their name and game and explains the broken ones")
{
    const Application app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    write(dir.filePath(QStringLiteral("shooter.json")), kShooter);
    write(dir.filePath(QStringLiteral("racer.json")), kRacer);
    write(dir.filePath(QStringLiteral("broken.json")), kBroken);
    write(dir.filePath(QStringLiteral("notes.txt")), "not a profile");
    QStringList discarded;
    const ProfileLibrary library(dir.path().toStdWString(), deleting(discarded));

    const QVariantList entries = library.entries();
    REQUIRE(entries.size() == 3);
    const QVariantMap broken = entries[0].toMap();
    CHECK(broken[QStringLiteral("id")].toString() == QStringLiteral("broken"));
    CHECK_FALSE(broken[QStringLiteral("error")].toString().isEmpty());
    CHECK(library.profile(QStringLiteral("broken")) == nullptr);

    const QVariantMap racer = entries[1].toMap();
    CHECK(racer[QStringLiteral("name")].toString() == QStringLiteral("Racer"));
    CHECK(racer[QStringLiteral("portrait")].toBool());

    const QVariantMap shooter = entries[2].toMap();
    CHECK(shooter[QStringLiteral("app")].toString() == QStringLiteral("com.example.shooter"));
    CHECK(shooter[QStringLiteral("controls")].toInt() == 2);
    CHECK(shooter[QStringLiteral("error")].toString().isEmpty());
    CHECK(shooter[QStringLiteral("markers")].toList().size() == 2);
    CHECK(library.profile(QStringLiteral("shooter")) != nullptr);
    CHECK(library.usableCount() == 2);
    CHECK(library.isProfileFile(QUrl::fromLocalFile(dir.filePath(QStringLiteral("x.JSON")))));
    CHECK_FALSE(library.isProfileFile(QUrl::fromLocalFile(dir.filePath(QStringLiteral("x.txt")))));
    CHECK_FALSE(library.isProfileFile(QUrl(QStringLiteral("https://example.com/x.json"))));
}

TEST_CASE("importing copies usable profiles under a free name and refuses the rest with a reason")
{
    const Application app;
    QTemporaryDir dir;
    QTemporaryDir source;
    REQUIRE(dir.isValid());
    REQUIRE(source.isValid());
    write(dir.filePath(QStringLiteral("shooter.json")), kDriving);
    write(source.filePath(QStringLiteral("shooter.json")), kShooter);
    write(source.filePath(QStringLiteral("broken.json")), kBroken);
    QStringList discarded;
    ProfileLibrary library(dir.path().toStdWString(), deleting(discarded));
    int entries = 0;
    QObject::connect(&library, &ProfileLibrary::entriesChanged, [&entries] { ++entries; });

    const QVariantMap result = library.importFiles({QUrl::fromLocalFile(source.filePath(QStringLiteral("shooter.json"))),
                                                    QUrl::fromLocalFile(source.filePath(QStringLiteral("broken.json"))),
                                                    QUrl(QStringLiteral("https://example.com/remote.json"))});
    CHECK(result[QStringLiteral("imported")].toStringList() == QStringList{QStringLiteral("Shooter")});
    const QVariantList failed = result[QStringLiteral("failed")].toList();
    REQUIRE(failed.size() == 2);
    CHECK(failed[0].toMap()[QStringLiteral("file")].toString() == QStringLiteral("broken.json"));
    CHECK_FALSE(failed[0].toMap()[QStringLiteral("reason")].toString().isEmpty());
    CHECK(QFile::exists(dir.filePath(QStringLiteral("shooter-2.json"))));
    CHECK(library.find(QStringLiteral("shooter-2")) != nullptr);
    CHECK(entries >= 1);

    const QVariantMap again =
        library.importFiles({QUrl::fromLocalFile(dir.filePath(QStringLiteral("shooter-2.json")))});
    CHECK(again[QStringLiteral("imported")].toStringList() == QStringList{QStringLiteral("Shooter")});
    CHECK(library.all().size() == 2);
}

TEST_CASE("profiles are duplicated, exported and removed, and cycling stays within one game")
{
    const Application app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    write(dir.filePath(QStringLiteral("shooter.json")), kShooter);
    write(dir.filePath(QStringLiteral("driving.json")), kDriving);
    write(dir.filePath(QStringLiteral("racer.json")), kRacer);
    QStringList discarded;
    ProfileLibrary library(dir.path().toStdWString(), deleting(discarded));

    CHECK(library.neighbour(QStringLiteral("shooter"), 1) == QStringLiteral("driving"));
    CHECK(library.neighbour(QStringLiteral("driving"), 1) == QStringLiteral("shooter"));
    CHECK(library.neighbour(QStringLiteral("driving"), -1) == QStringLiteral("shooter"));
    CHECK(library.neighbour(QStringLiteral("racer"), 1) == QStringLiteral("racer"));
    CHECK(library.neighbour({}, 1) == QStringLiteral("driving"));
    CHECK(library.neighbour({}, -1) == QStringLiteral("shooter"));

    const QString copy = library.duplicate(QStringLiteral("shooter"));
    CHECK(copy == QStringLiteral("shooter-copy"));
    REQUIRE(library.find(copy) != nullptr);
    CHECK(library.find(copy)->name == QStringLiteral("Shooter copy"));
    REQUIRE(library.profile(copy) != nullptr);
    CHECK(library.profile(copy)->controls.size() == 2);
    CHECK(library.duplicate(QStringLiteral("missing")).isEmpty());

    QTemporaryDir out;
    REQUIRE(out.isValid());
    CHECK(library.exportTo(QStringLiteral("shooter"), QUrl::fromLocalFile(out.filePath(QStringLiteral("x.json")))));
    QFile exported(out.filePath(QStringLiteral("x.json")));
    REQUIRE(exported.open(QIODevice::ReadOnly));
    CHECK(exported.readAll() == QByteArray(kShooter));

    CHECK(library.remove(copy));
    CHECK(discarded.size() == 1);
    CHECK(library.find(copy) == nullptr);
    CHECK_FALSE(library.remove(QStringLiteral("missing")));
}

TEST_CASE("reloading notices a profile whose file changed")
{
    const Application app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    write(dir.filePath(QStringLiteral("shooter.json")), kShooter);
    QStringList discarded;
    ProfileLibrary library(dir.path().toStdWString(), deleting(discarded));
    QStringList changed;
    QObject::connect(&library, &ProfileLibrary::profileChanged, [&changed](const QString& id) { changed.append(id); });

    library.reload();
    CHECK(changed.isEmpty());
    write(dir.filePath(QStringLiteral("shooter.json")), kDriving);
    library.reload();
    CHECK(changed == QStringList{QStringLiteral("shooter")});
    CHECK(library.find(QStringLiteral("shooter"))->name == QStringLiteral("Driving"));
}

TEST_CASE("saving writes the edited profile over its file and announces the change")
{
    const Application app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    write(dir.filePath(QStringLiteral("shooter.json")), kShooter);
    QStringList discarded;
    ProfileLibrary library(dir.path().toStdWString(), deleting(discarded));
    QStringList changed;
    QObject::connect(&library, &ProfileLibrary::profileChanged, [&changed](const QString& id) { changed.append(id); });

    gs::input::Profile edited = *library.profile(QStringLiteral("shooter"));
    edited.meta.name = "Shooter edited";
    edited.controls.pop_back();
    CHECK(library.save(QStringLiteral("shooter"), edited));
    CHECK(changed == QStringList{QStringLiteral("shooter")});
    const auto* entry = library.find(QStringLiteral("shooter"));
    REQUIRE(entry != nullptr);
    CHECK(entry->name == QStringLiteral("Shooter edited"));
    CHECK(entry->controls == 1);
    CHECK_FALSE(library.save(QStringLiteral("missing"), edited));
}

TEST_CASE("profiles from extensions come after your own, stay read-only and are copied as they are (PRF-006)")
{
    const Application app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    write(dir.filePath(QStringLiteral("shooter.json")), kShooter);
    QStringList discarded;
    ProfileLibrary library(dir.path().toStdWString(), deleting(discarded));
    QStringList changed;
    QObject::connect(&library, &ProfileLibrary::profileChanged, [&changed](const QString& id) { changed.append(id); });
    const QString id = QStringLiteral("alice.pack/standard");
    gs::app::ExtensionProfile contributed{.id = id,
                                          .extensionId = QStringLiteral("alice.pack"),
                                          .extensionName = QStringLiteral("Alice pack"),
                                          .version = QStringLiteral("1.0.0"),
                                          .json = QByteArray(kDriving),
                                          .patchedBy = {QStringLiteral("Bob lean")},
                                          .conflicts = {}};
    library.setExtensionProfiles({contributed});

    const QVariantList entries = library.entries();
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].toMap()[QStringLiteral("id")].toString() == QStringLiteral("shooter"));
    const QVariantMap entry = entries[1].toMap();
    CHECK(entry[QStringLiteral("id")].toString() == id);
    CHECK(entry[QStringLiteral("name")].toString() == QStringLiteral("Driving"));
    CHECK(entry[QStringLiteral("readOnly")].toBool());
    CHECK(entry[QStringLiteral("extensionName")].toString() == QStringLiteral("Alice pack"));
    CHECK(entry[QStringLiteral("version")].toString() == QStringLiteral("1.0.0"));
    CHECK(entry[QStringLiteral("patchedBy")].toStringList() == QStringList{QStringLiteral("Bob lean")});
    CHECK_FALSE(entries[0].toMap()[QStringLiteral("readOnly")].toBool());
    CHECK(library.profile(id) != nullptr);
    CHECK(library.usableCount() == 2);
    REQUIRE(library.linkedTo(QStringLiteral("com.example.shooter")) != nullptr);
    CHECK(library.linkedTo(QStringLiteral("com.example.shooter"))->id == QStringLiteral("shooter"));
    CHECK_FALSE(library.setApp(id, {}));
    CHECK_FALSE(library.remove(id));
    CHECK(discarded.isEmpty());

    const QString copy = library.duplicate(id);
    CHECK(copy == QStringLiteral("standard-copy"));
    REQUIRE(library.find(copy) != nullptr);
    CHECK_FALSE(library.find(copy)->readOnly());
    CHECK(library.find(copy)->name == QStringLiteral("Driving copy"));
    CHECK(library.find(copy)->app == QStringLiteral("com.example.shooter"));
    CHECK(QFile::exists(dir.filePath(QStringLiteral("standard-copy.json"))));

    contributed.json = QByteArray(kGeneral);
    library.setExtensionProfiles({contributed});
    CHECK(changed.contains(id));
    CHECK(library.find(id)->name == QStringLiteral("General"));

    library.setExtensionProfiles({});
    CHECK(library.find(id) == nullptr);
    CHECK(library.find(copy) != nullptr);
}

TEST_CASE("a profile is linked to a game in its file and found by the game's package")
{
    const Application app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    write(dir.filePath(QStringLiteral("shooter.json")), kShooter);
    write(dir.filePath(QStringLiteral("driving.json")), kDriving);
    write(dir.filePath(QStringLiteral("general.json")), kGeneral);
    write(dir.filePath(QStringLiteral("broken.json")), kBroken);
    QStringList discarded;
    ProfileLibrary library(dir.path().toStdWString(), deleting(discarded));

    REQUIRE(library.linkedTo(QStringLiteral("com.example.shooter")) != nullptr);
    CHECK(library.linkedTo(QStringLiteral("com.example.shooter"))->id == QStringLiteral("driving"));
    CHECK(library.linkedTo(QStringLiteral("com.example.other")) == nullptr);
    CHECK(library.linkedTo({}) == nullptr);

    CHECK(library.setApp(QStringLiteral("general"), QStringLiteral("com.example.other")));
    REQUIRE(library.linkedTo(QStringLiteral("com.example.other")) != nullptr);
    CHECK(library.linkedTo(QStringLiteral("com.example.other"))->id == QStringLiteral("general"));
    QFile file(dir.filePath(QStringLiteral("general.json")));
    REQUIRE(file.open(QIODevice::ReadOnly));
    CHECK(file.readAll().contains("com.example.other"));
    file.close();

    CHECK(library.setApp(QStringLiteral("general"), {}));
    CHECK(library.linkedTo(QStringLiteral("com.example.other")) == nullptr);
    CHECK(library.find(QStringLiteral("general"))->app.isEmpty());
    CHECK_FALSE(library.setApp(QStringLiteral("missing"), QStringLiteral("com.example.other")));
    CHECK_FALSE(library.setApp(QStringLiteral("broken"), QStringLiteral("com.example.other")));
    CHECK_FALSE(library.setApp(QStringLiteral("general"), QString(256, QLatin1Char('a'))));
}

TEST_CASE("the game in front picks the profile chosen for it, then the one linked to it")
{
    const Application app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    write(dir.filePath(QStringLiteral("shooter.json")), kShooter);
    write(dir.filePath(QStringLiteral("driving.json")), kDriving);
    write(dir.filePath(QStringLiteral("racer.json")), kRacer);
    write(dir.filePath(QStringLiteral("general.json")), kGeneral);
    QStringList discarded;
    const ProfileLibrary library(dir.path().toStdWString(), deleting(discarded));
    const QString shooterGame = QStringLiteral("com.example.shooter");
    const QString racerGame = QStringLiteral("com.example.racer");
    const QString launcher = QStringLiteral("com.example.launcher");

    SUBCASE("a linked profile is used when nothing was chosen for the game")
    {
        const auto choice = gs::app::chooseForApp(library, {}, racerGame, {});
        CHECK(choice.change);
        CHECK(choice.id == QStringLiteral("racer"));
        CHECK_FALSE(gs::app::chooseForApp(library, {}, racerGame, QStringLiteral("racer")).change);
    }
    SUBCASE("the profile chosen for the game wins over the linked one")
    {
        const Remembered remembered{{shooterGame.toStdString(), "shooter"}};
        const auto choice = gs::app::chooseForApp(library, remembered, shooterGame, QStringLiteral("racer"));
        CHECK(choice.change);
        CHECK(choice.id == QStringLiteral("shooter"));
        const Remembered general{{shooterGame.toStdString(), "general"}};
        CHECK(gs::app::chooseForApp(library, general, shooterGame, {}).id == QStringLiteral("general"));
    }
    SUBCASE("choosing no profile for a game is remembered too")
    {
        const Remembered remembered{{racerGame.toStdString(), ""}};
        const auto choice = gs::app::chooseForApp(library, remembered, racerGame, QStringLiteral("racer"));
        CHECK(choice.change);
        CHECK(choice.id.isEmpty());
        CHECK_FALSE(gs::app::chooseForApp(library, remembered, racerGame, {}).change);
    }
    SUBCASE("a remembered profile that was deleted falls back to the linked one")
    {
        const Remembered remembered{{racerGame.toStdString(), "gone"}};
        CHECK(gs::app::chooseForApp(library, remembered, racerGame, {}).id == QStringLiteral("racer"));
    }
    SUBCASE("leaving a game drops its profile but keeps a general one")
    {
        const auto left = gs::app::chooseForApp(library, {}, launcher, QStringLiteral("racer"));
        CHECK(left.change);
        CHECK(left.id.isEmpty());
        CHECK_FALSE(gs::app::chooseForApp(library, {}, launcher, QStringLiteral("general")).change);
        CHECK_FALSE(gs::app::chooseForApp(library, {}, launcher, {}).change);
        CHECK_FALSE(gs::app::chooseForApp(library, {}, {}, QStringLiteral("racer")).change);
    }
}

TEST_CASE("the start panel is offered for a game without a profile until it is closed (ADR-0072)")
{
    const Application app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    write(dir.filePath(QStringLiteral("racer.json")), kRacer);
    write(dir.filePath(QStringLiteral("general.json")), kGeneral);
    write(dir.filePath(QStringLiteral("broken.json")), kBroken);
    QStringList discarded;
    ProfileLibrary library(dir.path().toStdWString(), deleting(discarded));
    const QString newGame = QStringLiteral("com.example.new");
    const QString racerGame = QStringLiteral("com.example.racer");
    const std::vector<std::string> none;

    SUBCASE("a game the phone marks is offered, other apps are not")
    {
        CHECK(gs::app::offersStart(library, {}, none, newGame, true));
        CHECK_FALSE(gs::app::offersStart(library, {}, none, newGame, false));
        CHECK_FALSE(gs::app::offersStart(library, {}, none, {}, true));
    }
    SUBCASE("a game with a linked profile, or one chosen for it on this phone, has its start")
    {
        CHECK_FALSE(gs::app::offersStart(library, {}, none, racerGame, true));
        const Remembered general{{newGame.toStdString(), "general"}};
        CHECK_FALSE(gs::app::offersStart(library, general, none, newGame, true));
    }
    SUBCASE("choosing no profile for the game is a decision too")
    {
        const Remembered nothing{{newGame.toStdString(), ""}};
        CHECK_FALSE(gs::app::offersStart(library, nothing, none, newGame, true));
    }
    SUBCASE("an app the key mapping was used in counts as a game once its profile is gone")
    {
        const Remembered gone{{newGame.toStdString(), "deleted"}};
        CHECK(gs::app::offersStart(library, gone, none, newGame, false));
        const Remembered broken{{newGame.toStdString(), "broken"}};
        CHECK(gs::app::offersStart(library, broken, none, newGame, false));
    }
    SUBCASE("closing it for a game keeps it closed for that game only")
    {
        const std::vector<std::string> closed{newGame.toStdString()};
        CHECK_FALSE(gs::app::offersStart(library, {}, closed, newGame, true));
        const Remembered gone{{newGame.toStdString(), "deleted"}};
        CHECK_FALSE(gs::app::offersStart(library, gone, closed, newGame, false));
        CHECK(gs::app::offersStart(library, {}, closed, QStringLiteral("com.example.other"), true));
    }
    SUBCASE("a profile linked to the game later ends the offer")
    {
        REQUIRE(gs::app::offersStart(library, {}, none, newGame, true));
        REQUIRE(library.setApp(QStringLiteral("general"), newGame));
        CHECK_FALSE(gs::app::offersStart(library, {}, none, newGame, true));
    }
}

TEST_CASE("edits of a profile from an extension are kept as your patch, survive its update and are restored (EDT-017)")
{
    const Application app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QStringList discarded;
    ProfileLibrary library(dir.path().toStdWString(), deleting(discarded));
    QStringList changed;
    QObject::connect(&library, &ProfileLibrary::profileChanged, [&changed](const QString& id) { changed.append(id); });
    const QString id = QStringLiteral("alice.pack/standard");
    gs::app::ExtensionProfile contributed{.id = id,
                                          .extensionId = QStringLiteral("alice.pack"),
                                          .extensionName = QStringLiteral("Alice pack"),
                                          .version = QStringLiteral("1.0.0"),
                                          .json = QByteArray(kShooter),
                                          .patchedBy = {},
                                          .conflicts = {}};
    library.setExtensionProfiles({contributed});
    REQUIRE(library.profile(id) != nullptr);
    CHECK_FALSE(library.find(id)->userPatch);

    gs::input::Profile edited = *library.profile(id);
    press(edited, "fire").input = *gs::input::parseChord("KeyF");
    control(edited, "move").offset = {0.4, -0.2};
    gs::input::Control lean;
    lean.id = "lean";
    lean.kind = gs::input::HoldControl{.press = gs::input::Binding{.input = *gs::input::parseChord("KeyQ")}};
    edited.controls.push_back(lean);
    REQUIRE(library.save(id, edited));

    const QString patch = dir.filePath(QStringLiteral("patches/alice.pack/standard.json"));
    CHECK(QFile::exists(patch));
    CHECK(changed == QStringList{id});
    const auto* entry = library.find(id);
    REQUIRE(entry != nullptr);
    CHECK(entry->readOnly());
    CHECK(entry->userPatch);
    CHECK(entry->userEdits == 3);
    CHECK(entry->userPatchProblem.isEmpty());
    CHECK(entry->modified.isValid());
    CHECK(*library.profile(id) == edited);
    CHECK(library.all().size() == 1);
    const QVariantMap listed = library.entries().back().toMap();
    CHECK(listed[QStringLiteral("userPatch")].toBool());
    CHECK(listed[QStringLiteral("userEdits")].toInt() == 3);

    ProfileLibrary reopened(dir.path().toStdWString(), deleting(discarded));
    reopened.setExtensionProfiles({contributed});
    REQUIRE(reopened.profile(id) != nullptr);
    CHECK(*reopened.profile(id) == edited);

    const QString copy = library.duplicate(id);
    REQUIRE(library.profile(copy) != nullptr);
    CHECK(controlIds(*library.profile(copy)) == std::vector<std::string>{"fire", "move", "lean"});
    CHECK(library.remove(copy));

    contributed.json = QByteArray(kShooterUpdate);
    contributed.version = QStringLiteral("1.1.0");
    library.setExtensionProfiles({contributed});
    REQUIRE(library.profile(id) != nullptr);
    gs::input::Profile updated = *library.profile(id);
    CHECK(controlIds(updated) == std::vector<std::string>{"fire", "move", "map", "lean"});
    REQUIRE(press(updated, "fire").input.has_value());
    CHECK(gs::input::chordName(*press(updated, "fire").input) == "KeyF");
    CHECK(control(updated, "fire").anchor == gs::input::Anchor::Right);
    CHECK(control(updated, "fire").offset == gs::input::Vec2{-0.3, 0.1});
    CHECK(control(updated, "move").offset == gs::input::Vec2{0.4, -0.2});
    CHECK(std::get<gs::input::WasdControl>(control(updated, "move").kind).down.has_value());
    CHECK(library.find(id)->userEdits == 3);

    CHECK(library.restore(id));
    CHECK_FALSE(QFile::exists(patch));
    CHECK_FALSE(library.find(id)->userPatch);
    CHECK(*library.profile(id) == loaded(kShooterUpdate));
    CHECK_FALSE(library.restore(id));

    REQUIRE(library.save(id, updated));
    CHECK(QFile::exists(patch));
    REQUIRE(library.save(id, loaded(kShooterUpdate)));
    CHECK_FALSE(QFile::exists(patch));
    CHECK_FALSE(library.find(id)->userPatch);
    CHECK(discarded.size() == 1);
}

TEST_CASE("handing the gamepad to the game in a profile from an extension is one edit of yours (HID-007)")
{
    const Application app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QStringList discarded;
    ProfileLibrary library(dir.path().toStdWString(), deleting(discarded));
    const QString id = QStringLiteral("alice.pack/standard");
    const gs::app::ExtensionProfile contributed{.id = id,
                                                .extensionId = QStringLiteral("alice.pack"),
                                                .extensionName = QStringLiteral("Alice pack"),
                                                .version = QStringLiteral("1.0.0"),
                                                .json = QByteArray(kShooter),
                                                .patchedBy = {},
                                                .conflicts = {}};
    library.setExtensionProfiles({contributed});
    REQUIRE(library.profile(id) != nullptr);
    REQUIRE(library.profile(id)->gamepad == gs::input::GamepadMode::Map);

    gs::input::Profile edited = *library.profile(id);
    edited.gamepad = gs::input::GamepadMode::Passthrough;
    REQUIRE(library.save(id, edited));
    const auto* entry = library.find(id);
    REQUIRE(entry != nullptr);
    CHECK(entry->userPatch);
    CHECK(entry->userEdits == 1);
    CHECK(*library.profile(id) == edited);

    ProfileLibrary reopened(dir.path().toStdWString(), deleting(discarded));
    reopened.setExtensionProfiles({contributed});
    REQUIRE(reopened.profile(id) != nullptr);
    CHECK(reopened.profile(id)->gamepad == gs::input::GamepadMode::Passthrough);

    edited.gamepad = gs::input::GamepadMode::Map;
    REQUIRE(library.save(id, edited));
    CHECK_FALSE(library.find(id)->userPatch);
    CHECK(*library.profile(id) == loaded(kShooter));
}

TEST_CASE("a patch of yours that no longer fits the profile from the extension is shown as a problem")
{
    const Application app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    REQUIRE(QDir(dir.path()).mkpath(QStringLiteral("patches/alice.pack")));
    write(dir.filePath(QStringLiteral("patches/alice.pack/standard.json")), kBrokenPatch);
    QStringList discarded;
    ProfileLibrary library(dir.path().toStdWString(), deleting(discarded));
    const QString id = QStringLiteral("alice.pack/standard");
    library.setExtensionProfiles({{.id = id,
                                   .extensionId = QStringLiteral("alice.pack"),
                                   .extensionName = QStringLiteral("Alice pack"),
                                   .version = QStringLiteral("1.0.0"),
                                   .json = QByteArray(kShooter),
                                   .patchedBy = {},
                                   .conflicts = {}}});
    const auto* entry = library.find(id);
    REQUIRE(entry != nullptr);
    CHECK(entry->userPatch);
    CHECK(entry->userEdits == 0);
    CHECK_FALSE(entry->userPatchProblem.isEmpty());
    REQUIRE(library.profile(id) != nullptr);
    CHECK(*library.profile(id) == loaded(kShooter));
    CHECK(library.restore(id));
    CHECK(library.find(id)->userPatchProblem.isEmpty());
}

TEST_CASE("a profile folder is listed with its logic, wins over a file of the same name and is linked, copied and removed whole (ADR-0081)")
{
    const Application app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QDir root(dir.path());
    makeDemo(root, QStringLiteral("demo"));
    REQUIRE(root.mkpath(QStringLiteral("racer")));
    write(root.filePath(QStringLiteral("racer/profile.json")), kRacer);
    write(root.filePath(QStringLiteral("racer.json")), kGeneral);
    REQUIRE(root.mkpath(QStringLiteral(".hidden")));
    write(root.filePath(QStringLiteral(".hidden/profile.json")), kShooter);
    REQUIRE(root.mkpath(QStringLiteral("notes")));
    write(root.filePath(QStringLiteral("notes/readme.txt")), "not a profile");
    write(root.filePath(QStringLiteral("shooter.json")), kShooter);
    QStringList discarded;
    ProfileLibrary library(dir.path().toStdWString(), deletingFolders(discarded));

    const QVariantList entries = library.entries();
    REQUIRE(entries.size() == 3);
    const QVariantMap demo = entries[0].toMap();
    CHECK(demo[QStringLiteral("id")].toString() == QStringLiteral("demo"));
    CHECK(demo[QStringLiteral("name")].toString() == QStringLiteral("Logic demo"));
    CHECK(demo[QStringLiteral("folder")].toBool());
    CHECK(demo[QStringLiteral("logic")].toBool());
    const QVariantMap racer = entries[1].toMap();
    CHECK(racer[QStringLiteral("id")].toString() == QStringLiteral("racer"));
    CHECK(racer[QStringLiteral("name")].toString() == QStringLiteral("Racer"));
    CHECK(racer[QStringLiteral("folder")].toBool());
    CHECK_FALSE(racer[QStringLiteral("logic")].toBool());
    const QVariantMap shooter = entries[2].toMap();
    CHECK(shooter[QStringLiteral("id")].toString() == QStringLiteral("shooter"));
    CHECK_FALSE(shooter[QStringLiteral("folder")].toBool());
    CHECK_FALSE(shooter[QStringLiteral("logic")].toBool());

    const gs::app::ProfileEntry* entry = library.find(QStringLiteral("demo"));
    REQUIRE(entry != nullptr);
    CHECK(leafOf(entry->folder) == QStringLiteral("demo"));
    CHECK(leafOf(entry->path) == QStringLiteral("profile.json"));
    CHECK(gs::app::logicEntryOf(entry->folder) == "logic.ts");
    CHECK(library.exportName(QStringLiteral("demo")) == QStringLiteral("demo.gsprofile"));
    CHECK(library.exportName(QStringLiteral("shooter")) == QStringLiteral("shooter.json"));
    CHECK(library.exportName(QStringLiteral("missing")).isEmpty());
    CHECK(library.isProfileFile(QUrl::fromLocalFile(root.filePath(QStringLiteral("demo")))));
    CHECK(library.isProfileFile(QUrl::fromLocalFile(root.filePath(QStringLiteral("x.GSPROFILE")))));
    CHECK_FALSE(library.isProfileFile(QUrl::fromLocalFile(root.filePath(QStringLiteral("notes")))));

    REQUIRE(library.setApp(QStringLiteral("demo"), QStringLiteral("com.example.demo")));
    QFile linked(root.filePath(QStringLiteral("demo/profile.json")));
    REQUIRE(linked.open(QIODevice::ReadOnly));
    CHECK(linked.readAll().contains("com.example.demo"));
    linked.close();
    REQUIRE(library.linkedTo(QStringLiteral("com.example.demo")) != nullptr);
    CHECK(library.linkedTo(QStringLiteral("com.example.demo"))->id == QStringLiteral("demo"));

    const QString copy = library.duplicate(QStringLiteral("demo"));
    CHECK(copy == QStringLiteral("demo-copy"));
    CHECK(QFile::exists(root.filePath(QStringLiteral("demo-copy/logic.ts"))));
    CHECK(QFile::exists(root.filePath(QStringLiteral("demo-copy/res/a.txt"))));
    CHECK_FALSE(QFileInfo::exists(root.filePath(QStringLiteral("demo-copy/node_modules"))));
    CHECK_FALSE(QFileInfo::exists(root.filePath(QStringLiteral("demo-copy/.git"))));
    REQUIRE(library.find(copy) != nullptr);
    CHECK(library.find(copy)->name == QStringLiteral("Logic demo copy"));
    CHECK(library.find(copy)->logic);
    CHECK(library.duplicate(QStringLiteral("racer")) == QStringLiteral("racer-copy"));
    CHECK(QFile::exists(root.filePath(QStringLiteral("racer-copy/profile.json"))));

    CHECK(library.remove(copy));
    REQUIRE(discarded.size() == 1);
    CHECK(QFileInfo(discarded[0]).fileName() == QStringLiteral("demo-copy"));
    CHECK_FALSE(QFileInfo::exists(root.filePath(QStringLiteral("demo-copy"))));
    CHECK(library.find(copy) == nullptr);
    CHECK(library.find(QStringLiteral("demo")) != nullptr);
}

TEST_CASE("a profile file becomes a folder of the same id when logic or pictures are added (ADR-0081)")
{
    const Application app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QDir root(dir.path());
    write(root.filePath(QStringLiteral("shooter.json")), kShooter);
    makeDemo(root, QStringLiteral("demo"));
    QStringList discarded;
    ProfileLibrary library(dir.path().toStdWString(), deletingFolders(discarded));
    REQUIRE(library.find(QStringLiteral("shooter")) != nullptr);
    REQUIRE(library.find(QStringLiteral("shooter"))->folder.empty());

    const QString folder = library.ensureFolder(QStringLiteral("shooter"));
    REQUIRE_FALSE(folder.isEmpty());
    CHECK(QFileInfo(folder).fileName() == QStringLiteral("shooter"));
    CHECK_FALSE(QFileInfo::exists(root.filePath(QStringLiteral("shooter.json"))));
    QFile moved(root.filePath(QStringLiteral("shooter/profile.json")));
    REQUIRE(moved.open(QIODevice::ReadOnly));
    CHECK(moved.readAll() == QByteArray(kShooter));
    moved.close();
    const gs::app::ProfileEntry* entry = library.find(QStringLiteral("shooter"));
    REQUIRE(entry != nullptr);
    CHECK(leafOf(entry->folder) == QStringLiteral("shooter"));
    CHECK(leafOf(entry->path) == QStringLiteral("profile.json"));
    CHECK_FALSE(entry->logic);
    REQUIRE(entry->profile != nullptr);
    CHECK(library.entries().size() == 2);

    CHECK(library.ensureFolder(QStringLiteral("shooter")) == folder);
    const QString demo = library.ensureFolder(QStringLiteral("demo"));
    CHECK(QFileInfo(demo).fileName() == QStringLiteral("demo"));
    CHECK(library.ensureFolder(QStringLiteral("missing")).isEmpty());
    CHECK(discarded.isEmpty());
}

TEST_CASE("a profile folder travels as a .gsprofile package with its logic and pictures, and folders are imported too (ADR-0081)")
{
    const Application app;
    QTemporaryDir dir;
    QTemporaryDir other;
    QTemporaryDir out;
    REQUIRE(dir.isValid());
    REQUIRE(other.isValid());
    REQUIRE(out.isValid());
    const QDir root(dir.path());
    makeDemo(root, QStringLiteral("demo"));
    QStringList discarded;
    ProfileLibrary library(dir.path().toStdWString(), deletingFolders(discarded));

    const QString package = out.filePath(QStringLiteral("demo.gsprofile"));
    REQUIRE(library.exportTo(QStringLiteral("demo"), QUrl::fromLocalFile(package)));
    CHECK(archived(package) == std::vector<std::string>{"logic.ts", "profile.json", "res/a.txt"});
    const QString keys = out.filePath(QStringLiteral("keys.json"));
    REQUIRE(library.exportTo(QStringLiteral("demo"), QUrl::fromLocalFile(keys)));
    QFile exported(keys);
    REQUIRE(exported.open(QIODevice::ReadOnly));
    CHECK(exported.readAll() == QByteArray(kLogicDemo));

    ProfileLibrary receiver(other.path().toStdWString(), deletingFolders(discarded));
    const QDir received(other.path());
    const QVariantMap asked = receiver.importFiles({QUrl::fromLocalFile(package)});
    CHECK(asked[QStringLiteral("imported")].toStringList().isEmpty());
    CHECK(asked[QStringLiteral("failed")].toList().isEmpty());
    const QVariantList withLogic = asked[QStringLiteral("logic")].toList();
    REQUIRE(withLogic.size() == 1);
    CHECK(withLogic[0].toMap()[QStringLiteral("file")].toString() == QStringLiteral("demo.gsprofile"));
    CHECK(withLogic[0].toMap()[QStringLiteral("url")].toUrl() == QUrl::fromLocalFile(package));
    CHECK_FALSE(QFileInfo::exists(received.filePath(QStringLiteral("demo"))));
    CHECK(receiver.all().empty());

    const QVariantMap first = receiver.importFiles({QUrl::fromLocalFile(package)}, true);
    CHECK(first[QStringLiteral("imported")].toStringList() == QStringList{QStringLiteral("Logic demo")});
    CHECK(first[QStringLiteral("failed")].toList().isEmpty());
    CHECK(first[QStringLiteral("logic")].toList().isEmpty());
    CHECK(QFile::exists(received.filePath(QStringLiteral("demo/logic.ts"))));
    CHECK(QFile::exists(received.filePath(QStringLiteral("demo/res/a.txt"))));
    CHECK_FALSE(QFileInfo::exists(received.filePath(QStringLiteral(".import-demo"))));
    REQUIRE(receiver.find(QStringLiteral("demo")) != nullptr);
    CHECK(receiver.find(QStringLiteral("demo"))->logic);

    const QVariantMap again = receiver.importFiles({QUrl::fromLocalFile(package)}, true);
    CHECK(again[QStringLiteral("imported")].toStringList() == QStringList{QStringLiteral("Logic demo")});
    CHECK(receiver.find(QStringLiteral("demo-2")) != nullptr);

    const QVariantMap unasked = receiver.importFiles({QUrl::fromLocalFile(root.filePath(QStringLiteral("demo")))});
    CHECK(unasked[QStringLiteral("imported")].toStringList().isEmpty());
    CHECK(unasked[QStringLiteral("logic")].toList().size() == 1);
    CHECK(receiver.all().size() == 2);
    const QVariantMap folder =
        receiver.importFiles({QUrl::fromLocalFile(root.filePath(QStringLiteral("demo")))}, true);
    CHECK(folder[QStringLiteral("imported")].toStringList() == QStringList{QStringLiteral("Logic demo")});
    CHECK(QFile::exists(received.filePath(QStringLiteral("demo-3/res/a.txt"))));
    CHECK_FALSE(QFileInfo::exists(received.filePath(QStringLiteral("demo-3/node_modules"))));
    CHECK_FALSE(QFileInfo::exists(received.filePath(QStringLiteral("demo-3/.git"))));
    REQUIRE(receiver.find(QStringLiteral("demo-3")) != nullptr);
    CHECK(receiver.find(QStringLiteral("demo-3"))->logic);

    const QVariantMap present = receiver.importFiles({QUrl::fromLocalFile(received.filePath(QStringLiteral("demo")))});
    CHECK(present[QStringLiteral("imported")].toStringList() == QStringList{QStringLiteral("Logic demo")});
    CHECK(receiver.all().size() == 3);

    const QDir spare(out.path());
    REQUIRE(spare.mkpath(QStringLiteral("empty")));
    write(spare.filePath(QStringLiteral("empty/readme.txt")), "no profile here");
    const QString bare = spare.filePath(QStringLiteral("bare.gsprofile"));
    REQUIRE(gs::ext::packDirectory(spare.filePath(QStringLiteral("empty")).toStdWString(), bare.toStdWString()).empty());
    const QString junk = spare.filePath(QStringLiteral("junk.gsprofile"));
    write(junk, "not a zip");
    const QVariantMap refused = receiver.importFiles({QUrl::fromLocalFile(bare), QUrl::fromLocalFile(junk),
                                                      QUrl::fromLocalFile(spare.filePath(QStringLiteral("empty")))});
    CHECK(refused[QStringLiteral("imported")].toStringList().isEmpty());
    const QVariantList failed = refused[QStringLiteral("failed")].toList();
    REQUIRE(failed.size() == 3);
    CHECK(failed[0].toMap()[QStringLiteral("reason")].toString() ==
          QStringLiteral("The package has no readable profile.json."));
    CHECK(failed[1].toMap()[QStringLiteral("reason")].toString() ==
          QStringLiteral("The package has no readable profile.json."));
    CHECK(failed[2].toMap()[QStringLiteral("reason")].toString() ==
          QStringLiteral("The folder has no readable profile.json."));
    CHECK(receiver.all().size() == 3);
}

TEST_CASE("an import cut short leaves no hidden folder behind once the library opens again (ADR-0081)")
{
    const Application app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QDir root(dir.path());
    REQUIRE(root.mkpath(QStringLiteral(".import-demo/res")));
    write(root.filePath(QStringLiteral(".import-demo/res/a.txt")), "half");
    REQUIRE(root.mkpath(QStringLiteral(".keep")));
    QStringList discarded;
    const ProfileLibrary library(dir.path().toStdWString(), deletingFolders(discarded));
    CHECK_FALSE(QFileInfo::exists(root.filePath(QStringLiteral(".import-demo"))));
    CHECK(QFileInfo::exists(root.filePath(QStringLiteral(".keep"))));
    CHECK(library.all().empty());
}

TEST_CASE("reloading keeps each profile object until its contents change and notices logic added to a folder (ADR-0081)")
{
    const Application app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QDir root(dir.path());
    write(root.filePath(QStringLiteral("shooter.json")), kShooter);
    REQUIRE(root.mkpath(QStringLiteral("demo")));
    write(root.filePath(QStringLiteral("demo/profile.json")), kLogicDemo);
    QStringList discarded;
    ProfileLibrary library(dir.path().toStdWString(), deletingFolders(discarded));
    const std::shared_ptr<const gs::input::Profile> shooter = library.profile(QStringLiteral("shooter"));
    const std::shared_ptr<const gs::input::Profile> demo = library.profile(QStringLiteral("demo"));
    REQUIRE(shooter != nullptr);
    REQUIRE(demo != nullptr);
    CHECK_FALSE(library.find(QStringLiteral("demo"))->logic);

    library.reload();
    CHECK(library.profile(QStringLiteral("shooter")) == shooter);
    write(root.filePath(QStringLiteral("demo/logic.ts")), "export function go(): void {}\n");
    library.reload();
    CHECK(library.profile(QStringLiteral("demo")) == demo);
    CHECK(library.find(QStringLiteral("demo"))->logic);

    write(root.filePath(QStringLiteral("shooter.json")), kDriving);
    library.reload();
    REQUIRE(library.profile(QStringLiteral("shooter")) != nullptr);
    CHECK(library.profile(QStringLiteral("shooter")) != shooter);
    CHECK(library.profile(QStringLiteral("demo")) == demo);
}

TEST_CASE("a profile folder copied in while running shows up once its profile.json arrives (RC1 D-026)")
{
    const Application app;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QDir root(dir.path());
    QStringList discarded;
    ProfileLibrary library(dir.path().toStdWString(), deletingFolders(discarded));
    int reloads = 0;
    QObject::connect(&library, &ProfileLibrary::entriesChanged, [&reloads] { ++reloads; });
    const auto settle = [](const std::function<bool()>& done) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!done() && std::chrono::steady_clock::now() < deadline) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        return done();
    };

    REQUIRE(root.mkpath(QStringLiteral("copied")));
    REQUIRE(settle([&reloads] { return reloads > 0; }));
    CHECK(library.find(QStringLiteral("copied")) == nullptr);
    write(root.filePath(QStringLiteral("copied/profile.json")), kLogicDemo);
    CHECK(settle([&library] { return library.find(QStringLiteral("copied")) != nullptr; }));
}

namespace {

constexpr const char* kComponentDemo = R"({"format":"gamescrcpy.profile","version":3,
    "meta":{"name":"Component demo"},
    "controls":[{"id":"combo","type":"component","component":"mine/combo","inputs":{"key":"KeyQ"}},
                {"id":"burst","type":"component","component":"examples/burst","inputs":{"key":"KeyB"}}]})";

std::filesystem::path pathOf(const QString& text)
{
    return {text.toStdWString()};
}

}

TEST_CASE("a profile using my components carries them in its package, and importing installs them and points at them (ADR-0091)")
{
    const Application app;
    QTemporaryDir sender;
    QTemporaryDir receiver;
    QTemporaryDir out;
    REQUIRE(sender.isValid());
    REQUIRE(receiver.isValid());
    REQUIRE(out.isValid());
    const QString package = out.filePath(QStringLiteral("demo.gsprofile"));
    QStringList discarded;
    {
        gs::app::ComponentLibrary components(pathOf(sender.filePath(QStringLiteral("examples"))),
                                             pathOf(sender.filePath(QStringLiteral("components"))));
        const QString ref = components.add(QStringLiteral("Combo"));
        REQUIRE(ref == QStringLiteral("mine/combo"));
        const QDir root(sender.filePath(QStringLiteral("profiles")));
        REQUIRE(root.mkpath(QStringLiteral("demo")));
        write(root.filePath(QStringLiteral("demo/profile.json")), kComponentDemo);
        ProfileLibrary library(pathOf(root.path()), deletingFolders(discarded));
        const QVariantList users = library.usersOf(ref);
        REQUIRE(users.size() == 1);
        CHECK(users[0].toMap()[QStringLiteral("id")].toString() == QStringLiteral("demo"));
        CHECK(users[0].toMap()[QStringLiteral("count")].toInt() == 1);
        CHECK(library.usersOf(QStringLiteral("mine/other")).isEmpty());
        REQUIRE(library.exportTo(QStringLiteral("demo"), QUrl::fromLocalFile(package)));
        CHECK(archived(package) == std::vector<std::string>{"components/combo/component.json",
                                                            "components/combo/component.ts",
                                                            "components/combo/tsconfig.json", "profile.json"});
        CHECK_FALSE(QFileInfo::exists(root.filePath(QStringLiteral("demo/components"))));
    }
    gs::app::ComponentLibrary components(pathOf(receiver.filePath(QStringLiteral("examples"))),
                                         pathOf(receiver.filePath(QStringLiteral("components"))));
    const QString theirs = components.add(QStringLiteral("Combo"));
    REQUIRE(components.find(theirs) != nullptr);
    write(QDir(components.find(theirs)->folder).filePath(QStringLiteral("component.ts")),
          "export default class Theirs extends Component {}\n");
    components.reload();
    ProfileLibrary library(pathOf(receiver.filePath(QStringLiteral("profiles"))), deletingFolders(discarded));
    const QVariantMap asked = library.importFiles({QUrl::fromLocalFile(package)});
    CHECK(asked[QStringLiteral("imported")].toStringList().isEmpty());
    CHECK(asked[QStringLiteral("logic")].toList().size() == 1);
    const QVariantMap imported = library.importFiles({QUrl::fromLocalFile(package)}, true);
    CHECK(imported[QStringLiteral("imported")].toStringList() == QStringList{QStringLiteral("Component demo")});
    const gs::app::ComponentEntry* added = components.find(QStringLiteral("mine/combo-2"));
    REQUIRE(added != nullptr);
    CHECK(added->name == QStringLiteral("Combo 2"));
    CHECK(components.find(theirs)->name == QStringLiteral("Combo"));
    const auto* entry = library.find(QStringLiteral("demo"));
    REQUIRE(entry != nullptr);
    REQUIRE(entry->profile != nullptr);
    CHECK(std::get<gs::input::ComponentControl>(entry->profile->controls[0].kind).component == "mine/combo-2");
    CHECK(std::get<gs::input::ComponentControl>(entry->profile->controls[1].kind).component == "examples/burst");
    CHECK_FALSE(QFileInfo::exists(QDir(receiver.filePath(QStringLiteral("profiles"))).filePath(QStringLiteral("demo/components"))));
    CHECK(library.usersOf(QStringLiteral("mine/combo-2")).size() == 1);
    CHECK(library.usersOf(theirs).isEmpty());
}
