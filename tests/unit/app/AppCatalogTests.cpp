#include "AppCatalog.h"

#include <doctest/doctest.h>

#include <QFile>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantMap>

#include <cstdint>
#include <filesystem>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using gs::app::AppAvailability;
using gs::app::AppCatalog;
using gs::app::AppItem;
using gs::app::AppListModel;
using gs::app::PackageAction;
namespace protocol = gs::protocol;

namespace {

struct FakePhone {
    AppAvailability availability{
        .name = QStringLiteral("Pixel 8"), .connected = true, .apps = true, .bulk = true, .independent = true};
    std::vector<std::uint32_t> lists;
    std::vector<std::pair<std::uint32_t, QString>> icons;
    QStringList launches;
    bool refuseIcons = false;
    std::vector<std::tuple<quint64, PackageAction, QString>> acts;
    std::vector<std::pair<quint64, QString>> installs;
    quint64 lastJob = 0;

    AppCatalog::Actions actions()
    {
        return {.act =
                    [this](const QString&, PackageAction action, const QString& package) {
                        acts.emplace_back(++lastJob, action, package);
                        return lastJob;
                    },
                .install =
                    [this](const QString&, const QString& apkPath) {
                        installs.emplace_back(++lastJob, apkPath);
                        return lastJob;
                    }};
    }

    AppCatalog::Source source()
    {
        return {.availability = [this](const QString&) { return availability; },
                .list =
                    [this](const QString&, std::uint32_t requestId) {
                        lists.push_back(requestId);
                        return true;
                    },
                .icon =
                    [this](const QString&, std::uint32_t requestId, const QString& package, int sizePx) {
                        CHECK(sizePx == AppCatalog::kIconSize);
                        if (refuseIcons) {
                            return false;
                        }
                        icons.emplace_back(requestId, package);
                        return true;
                    },
                .launch =
                    [this](const QString& serial, const QString& package, const QString& label, bool independent) {
                        launches.append(QStringLiteral("%1 %2 %3 %4")
                                            .arg(serial, package, label, independent ? u"independent" : u"phone"));
                    }};
    }
};

protocol::AppEntry entry(std::string package, std::string label, std::uint64_t versionCode, bool game = false,
                         bool system = false)
{
    protocol::AppEntry app;
    app.package = std::move(package);
    app.label = std::move(label);
    app.versionCode = versionCode;
    app.game = game;
    app.system = system;
    app.enabled = true;
    return app;
}

QByteArray png(char tag)
{
    QByteArray data("\x89PNG\r\n\x1a\n", 8);
    data.append(tag);
    return data;
}

QStringList labels(const AppListModel& model)
{
    QStringList shown;
    for (int row = 0; row < model.rowCount(); ++row) {
        shown.append(model.data(model.index(row), AppListModel::LabelRole).toString());
    }
    return shown;
}

QString iconOf(const AppCatalog& catalog, const QString& package)
{
    const AppItem* app = catalog.model().find(package);
    return app != nullptr ? app->icon : QString();
}

std::filesystem::path pathOf(const QTemporaryDir& dir)
{
    return std::filesystem::path(dir.path().toStdWString());
}

void writeFile(const std::filesystem::path& path, const QByteArray& data)
{
    QFile file(QString::fromStdWString(path.wstring()));
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(data);
}

protocol::AppEntry disabled(std::string package, std::string label)
{
    protocol::AppEntry app = entry(std::move(package), std::move(label), 1);
    app.enabled = false;
    return app;
}

QVariant planned(const AppCatalog& catalog, const char* action, bool batch, const char* key)
{
    return catalog.plan(QString::fromLatin1(action), batch).value(QString::fromLatin1(key));
}

quint64 jobAt(const FakePhone& phone, std::size_t index)
{
    REQUIRE(index < phone.acts.size());
    return std::get<0>(phone.acts[index]);
}

void watch(AppCatalog& catalog, QStringList& reports)
{
    QObject::connect(&catalog, &AppCatalog::reported, [&reports](const QString& text, const QString& tone) {
        reports.append(tone + QStringLiteral(" ") + text);
    });
}

}

TEST_CASE("icons are cached by package name and version code, and only for real package names")
{
    const std::filesystem::path directory = L"cache";
    const auto file = AppCatalog::iconFile(directory, QStringLiteral("com.example.game_2"), 412);
    REQUIRE(file.has_value());
    CHECK(file->filename().wstring() == L"com.example.game_2@412.png");
    CHECK(file->parent_path() == directory);
    CHECK_FALSE(AppCatalog::iconFile(directory, QStringLiteral("../evil"), 1).has_value());
    CHECK_FALSE(AppCatalog::iconFile(directory, QStringLiteral("a/b"), 1).has_value());
    CHECK_FALSE(AppCatalog::iconFile(directory, QString(), 1).has_value());
}

TEST_CASE("the app grid is sorted by name and narrowed by tab and search")
{
    AppListModel model;
    model.reset({{.package = QStringLiteral("com.b"), .label = QStringLiteral("beta"), .game = true},
                 {.package = QStringLiteral("com.a"), .label = QStringLiteral("Alpha")},
                 {.package = QStringLiteral("android.settings"), .label = QStringLiteral("Settings"), .system = true},
                 {.package = QStringLiteral("com.app10"), .label = QStringLiteral("App 10")},
                 {.package = QStringLiteral("com.app9"), .label = QStringLiteral("App 9")}});
    CHECK(labels(model) == QStringList{QStringLiteral("Alpha"), QStringLiteral("App 9"), QStringLiteral("App 10"),
                                       QStringLiteral("beta"), QStringLiteral("Settings")});
    CHECK(model.packageAt(0) == QStringLiteral("com.a"));
    CHECK(model.packageAt(5).isEmpty());

    model.setFilter(AppListModel::Filter::Games);
    CHECK(labels(model) == QStringList{QStringLiteral("beta")});
    model.setFilter(AppListModel::Filter::System);
    CHECK(labels(model) == QStringList{QStringLiteral("Settings")});
    model.setFilter(AppListModel::Filter::All);
    model.setQuery(QStringLiteral("  APP "));
    CHECK(labels(model) == QStringList{QStringLiteral("App 9"), QStringLiteral("App 10")});
    model.setQuery(QStringLiteral("android"));
    CHECK(labels(model) == QStringList{QStringLiteral("Settings")});
    model.setQuery(QString());
    CHECK(model.rowCount() == 5);

    const auto roles = model.roleNames();
    CHECK(roles.value(AppListModel::PackageRole) == "packageName");
    CHECK(roles.value(AppListModel::IconRole) == "iconUrl");
}

TEST_CASE("opening a phone lists its apps, then asks for the icons not cached yet, three at a time")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto cached = AppCatalog::iconFile(pathOf(dir), QStringLiteral("com.cached"), 3);
    REQUIRE(cached.has_value());
    writeFile(*cached, png('c'));
    FakePhone phone;
    AppCatalog catalog(pathOf(dir), phone.source());

    catalog.open(QStringLiteral("A"));
    CHECK(catalog.serial() == QStringLiteral("A"));
    CHECK(catalog.phoneName() == QStringLiteral("Pixel 8"));
    REQUIRE(phone.lists.size() == 1);
    CHECK(catalog.loading());
    CHECK(catalog.ready());
    CHECK(catalog.independentDisplays());
    CHECK(catalog.problem().isEmpty());

    catalog.listed(QStringLiteral("A"), phone.lists[0],
                   {entry("com.cached", "Cached", 3), entry("com.one", "One", 1, true), entry("com.two", "", 2),
                    entry("com.three", "Three", 3), entry("com.four", "Four", 4, false, true)});
    CHECK_FALSE(catalog.loading());
    CHECK(catalog.total() == 5);
    CHECK(catalog.games() == 1);
    CHECK(catalog.systemApps() == 1);
    CHECK(catalog.model().find(QStringLiteral("com.two"))->label == QStringLiteral("com.two"));
    CHECK(iconOf(catalog, QStringLiteral("com.cached")) ==
          QUrl::fromLocalFile(QString::fromStdWString(cached->wstring())).toString());
    REQUIRE(phone.icons.size() == AppCatalog::kIconsInFlight);
    CHECK(catalog.iconsInFlight() == AppCatalog::kIconsInFlight);

    const auto [requestId, package] = phone.icons[0];
    catalog.iconArrived(QStringLiteral("A"), requestId, package, png('1'));
    CHECK(phone.icons.size() == AppCatalog::kIconsInFlight + 1);
    const AppItem* answered = catalog.model().find(package);
    REQUIRE(answered != nullptr);
    const auto stored = AppCatalog::iconFile(pathOf(dir), package, answered->versionCode);
    REQUIRE(stored.has_value());
    CHECK(std::filesystem::exists(*stored));
    CHECK(answered->icon == QUrl::fromLocalFile(QString::fromStdWString(stored->wstring())).toString());

    for (std::size_t i = 1; i < phone.icons.size(); ++i) {
        catalog.iconArrived(QStringLiteral("A"), phone.icons[i].first, phone.icons[i].second, png('x'));
    }
    CHECK(catalog.iconsInFlight() == 0);
    for (const AppItem& app : catalog.model().all()) {
        CHECK_MESSAGE(!app.icon.isEmpty(), app.package.toStdString());
    }
}

TEST_CASE("an updated app replaces the icon cached for its old version, and garbage is not cached")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto old = AppCatalog::iconFile(pathOf(dir), QStringLiteral("com.game"), 1);
    const auto other = AppCatalog::iconFile(pathOf(dir), QStringLiteral("com.game.plus"), 1);
    writeFile(*old, png('o'));
    writeFile(*other, png('p'));
    FakePhone phone;
    AppCatalog catalog(pathOf(dir), phone.source());
    catalog.open(QStringLiteral("A"));
    catalog.listed(QStringLiteral("A"), phone.lists[0], {entry("com.game", "Game", 2), entry("com.bad", "Bad", 5)});
    REQUIRE(phone.icons.size() == 2);

    for (const auto& [requestId, package] : phone.icons) {
        catalog.iconArrived(QStringLiteral("A"), requestId, package,
                            package == QStringLiteral("com.bad") ? QByteArray("not a png") : png('n'));
    }
    CHECK(std::filesystem::exists(*AppCatalog::iconFile(pathOf(dir), QStringLiteral("com.game"), 2)));
    CHECK_FALSE(std::filesystem::exists(*old));
    CHECK(std::filesystem::exists(*other));
    CHECK_FALSE(std::filesystem::exists(*AppCatalog::iconFile(pathOf(dir), QStringLiteral("com.bad"), 5)));
    CHECK(iconOf(catalog, QStringLiteral("com.bad")).isEmpty());
}

TEST_CASE("a busy phone is asked again, a missing icon is not, and a failed list can be tried again")
{
    QTemporaryDir dir;
    FakePhone phone;
    AppCatalog catalog(pathOf(dir), phone.source());
    catalog.open(QStringLiteral("A"));
    catalog.requestFailed(QStringLiteral("A"), phone.lists[0], protocol::ErrorCode::InternalError,
                          QStringLiteral("package manager died"));
    CHECK_FALSE(catalog.loading());
    CHECK(catalog.canRetry());
    CHECK_FALSE(catalog.problem().isEmpty());

    catalog.reload();
    REQUIRE(phone.lists.size() == 2);
    CHECK_FALSE(catalog.canRetry());
    catalog.listed(QStringLiteral("A"), phone.lists[1], {entry("com.busy", "Busy", 1), entry("com.gone", "Gone", 1)});
    REQUIRE(phone.icons.size() == 2);
    const auto busy = phone.icons[0];
    const auto gone = phone.icons[1];
    catalog.requestFailed(QStringLiteral("A"), gone.first, protocol::ErrorCode::NotFound, QStringLiteral("gone"));
    catalog.requestFailed(QStringLiteral("A"), busy.first, protocol::ErrorCode::ResourceBusy, QStringLiteral("busy"));
    REQUIRE(phone.icons.size() == 3);
    CHECK(phone.icons[2].second == busy.second);
    catalog.requestFailed(QStringLiteral("A"), phone.icons[2].first, protocol::ErrorCode::ResourceBusy, {});
    REQUIRE(phone.icons.size() == 4);
    catalog.requestFailed(QStringLiteral("A"), phone.icons[3].first, protocol::ErrorCode::ResourceBusy, {});
    CHECK(phone.icons.size() == 4);
    CHECK(catalog.iconsInFlight() == 0);

    catalog.availabilityChanged(QStringLiteral("A"));
    CHECK(phone.icons.size() == 4);
}

TEST_CASE("replies for another phone or an old request are ignored, and a lost phone is asked again when back")
{
    QTemporaryDir dir;
    FakePhone phone;
    AppCatalog catalog(pathOf(dir), phone.source());
    catalog.open(QStringLiteral("A"));
    catalog.listed(QStringLiteral("B"), phone.lists[0], {entry("com.b", "B", 1)});
    catalog.listed(QStringLiteral("A"), phone.lists[0] + 7, {entry("com.old", "Old", 1)});
    CHECK(catalog.total() == 0);
    CHECK(catalog.loading());

    phone.availability.connected = false;
    catalog.availabilityChanged(QStringLiteral("A"));
    CHECK_FALSE(catalog.ready());
    CHECK_FALSE(catalog.independentDisplays());
    CHECK_FALSE(catalog.problem().isEmpty());
    catalog.listed(QStringLiteral("A"), phone.lists[0], {entry("com.late", "Late", 1)});
    CHECK(catalog.total() == 0);

    phone.availability.connected = true;
    phone.availability.bulk = false;
    catalog.availabilityChanged(QStringLiteral("A"));
    CHECK(phone.lists.size() == 1);
    CHECK(catalog.loading());
    phone.availability.bulk = true;
    catalog.availabilityChanged(QStringLiteral("A"));
    REQUIRE(phone.lists.size() == 2);
    catalog.listed(QStringLiteral("A"), phone.lists[1], {entry("com.a", "A app", 1)});
    CHECK(catalog.total() == 1);

    catalog.close();
    CHECK(catalog.serial().isEmpty());
    CHECK(catalog.total() == 0);
    CHECK(catalog.problem().isEmpty());
}

TEST_CASE("a phone whose server cannot list apps says so and is not asked")
{
    QTemporaryDir dir;
    FakePhone phone;
    phone.availability.apps = false;
    AppCatalog catalog(pathOf(dir), phone.source());
    catalog.open(QStringLiteral("A"));
    CHECK(phone.lists.empty());
    CHECK_FALSE(catalog.loading());
    CHECK_FALSE(catalog.problem().isEmpty());
}

TEST_CASE("launching names the app, and an independent display only where the phone has one")
{
    QTemporaryDir dir;
    FakePhone phone;
    AppCatalog catalog(pathOf(dir), phone.source());
    catalog.open(QStringLiteral("A"));
    catalog.listed(QStringLiteral("A"), phone.lists[0], {entry("com.game", "Game", 1, true)});

    catalog.launch(QStringLiteral("com.game"), false);
    catalog.launch(QStringLiteral("com.game"), true);
    catalog.launch(QStringLiteral("com.unknown"), false);
    phone.availability.independent = false;
    catalog.availabilityChanged(QStringLiteral("A"));
    CHECK_FALSE(catalog.independentDisplays());
    catalog.launch(QStringLiteral("com.game"), true);
    CHECK(phone.launches == QStringList{QStringLiteral("A com.game Game phone"),
                                        QStringLiteral("A com.game Game independent")});
}

TEST_CASE("the catalog's filter and query reach the grid")
{
    QTemporaryDir dir;
    FakePhone phone;
    AppCatalog catalog(pathOf(dir), phone.source());
    catalog.open(QStringLiteral("A"));
    catalog.listed(QStringLiteral("A"), phone.lists[0],
                   {entry("com.game", "Game", 1, true), entry("android.settings", "Settings", 1, false, true)});
    catalog.setFilter(1);
    CHECK(catalog.apps()->rowCount() == 1);
    catalog.setFilter(7);
    CHECK(catalog.filter() == 2);
    CHECK(catalog.apps()->rowCount() == 1);
    catalog.setFilter(0);
    catalog.setQuery(QStringLiteral("sett"));
    CHECK(catalog.apps()->rowCount() == 1);
    CHECK(catalog.apps()->data(catalog.apps()->index(0, 0), AppListModel::PackageRole).toString() ==
          QStringLiteral("android.settings"));
}

TEST_CASE("the chosen app offers what it allows, and a system app can only be disabled or enabled")
{
    QTemporaryDir dir;
    FakePhone phone;
    AppCatalog catalog(pathOf(dir), phone.source());
    catalog.setActions(phone.actions());
    catalog.open(QStringLiteral("A"));
    catalog.listed(QStringLiteral("A"), phone.lists[0],
                   {entry("com.game", "Game", 1, true), entry("android.settings", "Settings", 1, false, true)});
    CHECK(catalog.currentApp().isEmpty());

    catalog.setCurrent(QStringLiteral("com.game"));
    QVariantMap app = catalog.currentApp();
    CHECK(app.value(QStringLiteral("label")).toString() == QStringLiteral("Game"));
    CHECK(app.value(QStringLiteral("enabled")).toBool());
    CHECK(app.value(QStringLiteral("forceStop")).toBool());
    CHECK(app.value(QStringLiteral("disable")).toBool());
    CHECK_FALSE(app.value(QStringLiteral("enable")).toBool());
    CHECK(app.value(QStringLiteral("clearData")).toBool());
    CHECK(app.value(QStringLiteral("uninstall")).toBool());

    catalog.setCurrent(QStringLiteral("android.settings"));
    app = catalog.currentApp();
    CHECK(app.value(QStringLiteral("system")).toBool());
    CHECK(app.value(QStringLiteral("disable")).toBool());
    CHECK_FALSE(app.value(QStringLiteral("enable")).toBool());
    CHECK_FALSE(app.value(QStringLiteral("forceStop")).toBool());
    CHECK_FALSE(app.value(QStringLiteral("clearData")).toBool());
    CHECK_FALSE(app.value(QStringLiteral("uninstall")).toBool());
    CHECK(planned(catalog, "uninstall", false, "count").toInt() == 0);
    CHECK(catalog.run(QStringLiteral("uninstall"), {QStringLiteral("android.settings")}) == 0);
    CHECK(catalog.run(QStringLiteral("clearData"), {QStringLiteral("android.settings")}) == 0);
    CHECK(catalog.run(QStringLiteral("forceStop"), {QStringLiteral("android.settings")}) == 0);
    CHECK(phone.acts.empty());
    CHECK_FALSE(catalog.busy());

    CHECK(catalog.run(QStringLiteral("disable"), {QStringLiteral("android.settings")}) == 1);
    REQUIRE(phone.acts.size() == 1);
    CHECK(std::get<1>(phone.acts[0]) == PackageAction::Disable);
    CHECK(std::get<2>(phone.acts[0]) == QStringLiteral("android.settings"));
    CHECK(catalog.busy());
}

TEST_CASE("uninstalling or clearing one app asks first and names it, other actions on one app just run")
{
    QTemporaryDir dir;
    FakePhone phone;
    AppCatalog catalog(pathOf(dir), phone.source());
    catalog.setActions(phone.actions());
    catalog.open(QStringLiteral("A"));
    catalog.listed(QStringLiteral("A"), phone.lists[0], {entry("com.game", "Game", 1, true)});
    CHECK(planned(catalog, "uninstall", false, "count").toInt() == 0);

    catalog.setCurrent(QStringLiteral("com.game"));
    QVariantMap plan = catalog.plan(QStringLiteral("uninstall"), false);
    CHECK(plan.value(QStringLiteral("count")).toInt() == 1);
    CHECK(plan.value(QStringLiteral("confirm")).toBool());
    CHECK(plan.value(QStringLiteral("danger")).toBool());
    CHECK(plan.value(QStringLiteral("title")).toString() == AppCatalog::tr("Uninstall %1?").arg(QStringLiteral("Game")));
    CHECK(plan.value(QStringLiteral("accept")).toString() == AppCatalog::tr("Uninstall"));
    CHECK(plan.value(QStringLiteral("packages")).toStringList() == QStringList{QStringLiteral("com.game")});

    plan = catalog.plan(QStringLiteral("clearData"), false);
    CHECK(plan.value(QStringLiteral("confirm")).toBool());
    CHECK(plan.value(QStringLiteral("title")).toString() ==
          AppCatalog::tr("Clear the data of %1?").arg(QStringLiteral("Game")));

    plan = catalog.plan(QStringLiteral("forceStop"), false);
    CHECK(plan.value(QStringLiteral("count")).toInt() == 1);
    CHECK_FALSE(plan.value(QStringLiteral("confirm")).toBool());
    CHECK_FALSE(plan.value(QStringLiteral("danger")).toBool());
    CHECK(planned(catalog, "enable", false, "count").toInt() == 0);
    CHECK(catalog.plan(QStringLiteral("reboot"), false).isEmpty());
    CHECK(catalog.run(QStringLiteral("reboot"), {QStringLiteral("com.game")}) == 0);
    CHECK(phone.acts.empty());
}

TEST_CASE("a batch confirmation states how many apps it acts on and how many it skips")
{
    QTemporaryDir dir;
    FakePhone phone;
    AppCatalog catalog(pathOf(dir), phone.source());
    catalog.setActions(phone.actions());
    catalog.open(QStringLiteral("A"));
    catalog.listed(QStringLiteral("A"), phone.lists[0],
                   {entry("com.a", "A", 1), entry("com.b", "B", 1), entry("android.settings", "Settings", 1, false, true),
                    disabled("com.off", "Off")});
    const auto roles = catalog.apps()->roleNames();
    CHECK(roles.value(AppListModel::EnabledRole) == "appEnabled");
    CHECK(roles.value(AppListModel::CheckedRole) == "checked");

    catalog.setSelecting(true);
    CHECK(catalog.selecting());
    catalog.toggleChecked(QStringLiteral("com.a"));
    catalog.toggleChecked(QStringLiteral("com.b"));
    catalog.toggleChecked(QStringLiteral("android.settings"));
    catalog.toggleChecked(QStringLiteral("com.missing"));
    CHECK(catalog.checkedCount() == 3);
    CHECK(catalog.apps()->data(catalog.apps()->index(0, 0), AppListModel::CheckedRole).toBool());

    QVariantMap plan = catalog.plan(QStringLiteral("uninstall"), true);
    CHECK(plan.value(QStringLiteral("count")).toInt() == 2);
    CHECK(plan.value(QStringLiteral("skipped")).toInt() == 1);
    CHECK(plan.value(QStringLiteral("confirm")).toBool());
    CHECK(plan.value(QStringLiteral("danger")).toBool());
    CHECK(plan.value(QStringLiteral("title")).toString() == AppCatalog::tr("Uninstall %n app(s)?", "", 2));
    CHECK(plan.value(QStringLiteral("title")).toString().contains(u'2'));
    CHECK(plan.value(QStringLiteral("packages")).toStringList() ==
          QStringList{QStringLiteral("com.a"), QStringLiteral("com.b")});
    CHECK(plan.value(QStringLiteral("text")).toString().contains(AppCatalog::tr(
        "%n selected system app(s) are skipped: system apps can only be disabled or enabled.", "", 1)));

    plan = catalog.plan(QStringLiteral("disable"), true);
    CHECK(plan.value(QStringLiteral("count")).toInt() == 3);
    CHECK(plan.value(QStringLiteral("skipped")).toInt() == 0);
    CHECK(plan.value(QStringLiteral("confirm")).toBool());
    CHECK_FALSE(plan.value(QStringLiteral("danger")).toBool());
    CHECK(plan.value(QStringLiteral("title")).toString() == AppCatalog::tr("Disable %n app(s)?", "", 3));
    CHECK(planned(catalog, "enable", true, "count").toInt() == 0);

    QVariantMap eligible = catalog.eligible();
    CHECK(eligible.value(QStringLiteral("forceStop")).toInt() == 2);
    CHECK(eligible.value(QStringLiteral("disable")).toInt() == 3);
    CHECK(eligible.value(QStringLiteral("enable")).toInt() == 0);
    CHECK(eligible.value(QStringLiteral("clearData")).toInt() == 2);
    CHECK(eligible.value(QStringLiteral("uninstall")).toInt() == 2);

    catalog.toggleChecked(QStringLiteral("com.off"));
    plan = catalog.plan(QStringLiteral("enable"), true);
    CHECK(plan.value(QStringLiteral("count")).toInt() == 1);
    CHECK(plan.value(QStringLiteral("skipped")).toInt() == 3);
    CHECK(plan.value(QStringLiteral("title")).toString() == AppCatalog::tr("Enable %n app(s)?", "", 1));
    CHECK(plan.value(QStringLiteral("text")).toString().contains(
        AppCatalog::tr("%n selected app(s) are already enabled.", "", 3)));
    eligible = catalog.eligible();
    CHECK(eligible.value(QStringLiteral("forceStop")).toInt() == 2);
    CHECK(eligible.value(QStringLiteral("enable")).toInt() == 1);
    CHECK(eligible.value(QStringLiteral("uninstall")).toInt() == 3);
    plan = catalog.plan(QStringLiteral("forceStop"), true);
    CHECK(plan.value(QStringLiteral("count")).toInt() == 2);
    CHECK(plan.value(QStringLiteral("skipped")).toInt() == 2);
    CHECK(plan.value(QStringLiteral("text")).toString().contains(AppCatalog::tr(
        "%n selected app(s) are skipped: system apps and disabled apps cannot be force stopped.", "", 2)));

    catalog.toggleChecked(QStringLiteral("com.a"));
    CHECK(catalog.checkedCount() == 3);
    catalog.checkAll();
    CHECK(catalog.checkedCount() == 4);
    catalog.clearChecked();
    CHECK(catalog.checkedCount() == 0);
    catalog.setFilter(2);
    catalog.checkAll();
    CHECK(catalog.checkedCount() == 1);
    CHECK(catalog.plan(QStringLiteral("disable"), true).value(QStringLiteral("packages")).toStringList() ==
          QStringList{QStringLiteral("android.settings")});
    catalog.setFilter(0);
    catalog.setSelecting(false);
    CHECK_FALSE(catalog.selecting());
    CHECK(catalog.checkedCount() == 0);
}

TEST_CASE("a finished batch refreshes the list once and reports one summary")
{
    QStringList reports;
    QTemporaryDir dir;
    FakePhone phone;
    AppCatalog catalog(pathOf(dir), phone.source());
    watch(catalog, reports);
    catalog.setActions(phone.actions());
    catalog.open(QStringLiteral("A"));
    catalog.listed(QStringLiteral("A"), phone.lists[0], {entry("com.a", "A", 1), entry("com.b", "B", 1)});

    REQUIRE(catalog.run(QStringLiteral("uninstall"), {QStringLiteral("com.a"), QStringLiteral("com.b")}) == 2);
    CHECK(catalog.busy());
    CHECK(catalog.activity() == AppCatalog::tr("Uninstalling %n app(s)…", "", 2));
    CHECK(catalog.run(QStringLiteral("forceStop"), {QStringLiteral("com.a")}) == 0);
    REQUIRE(phone.acts.size() == 2);
    const std::size_t lists = phone.lists.size();

    catalog.actionFinished(jobAt(phone, 0), QStringLiteral("B"), QStringLiteral("com.a"), true, {});
    catalog.actionFinished(jobAt(phone, 0) + 100, QStringLiteral("A"), QStringLiteral("com.a"), true, {});
    CHECK(catalog.activity() == AppCatalog::tr("Uninstalling %n app(s)…", "", 2));
    catalog.actionFinished(jobAt(phone, 0), QStringLiteral("A"), QStringLiteral("com.a"), true, {});
    CHECK(catalog.busy());
    CHECK(catalog.activity() == AppCatalog::tr("Uninstalling %n app(s)…", "", 1));
    CHECK(phone.lists.size() == lists);
    CHECK(reports.isEmpty());

    catalog.actionFinished(jobAt(phone, 1), QStringLiteral("A"), QStringLiteral("com.b"), true, {});
    CHECK_FALSE(catalog.busy());
    CHECK(catalog.activity().isEmpty());
    REQUIRE(phone.lists.size() == lists + 1);
    CHECK(reports == QStringList{QStringLiteral("ok ") + AppCatalog::tr("Uninstalled %n app(s)", "", 2)});

    catalog.listed(QStringLiteral("A"), phone.lists.back(), {entry("com.c", "C", 1), entry("com.d", "D", 1)});
    CHECK(catalog.total() == 2);
    REQUIRE(catalog.run(QStringLiteral("forceStop"), {QStringLiteral("com.c"), QStringLiteral("com.d")}) == 2);
    catalog.actionFinished(jobAt(phone, 2), QStringLiteral("A"), QStringLiteral("com.c"), true, {});
    catalog.actionFinished(jobAt(phone, 3), QStringLiteral("A"), QStringLiteral("com.d"), false, QStringLiteral("Busy"));
    CHECK(reports.back() == QStringLiteral("warn ") + AppCatalog::tr("%n app(s) failed. %1: %2", "", 1)
                                                          .arg(QStringLiteral("D"), QStringLiteral("Busy")));

    REQUIRE(catalog.run(QStringLiteral("clearData"), {QStringLiteral("com.c")}) == 1);
    catalog.actionFinished(jobAt(phone, 4), QStringLiteral("A"), QStringLiteral("com.c"), false,
                           QStringLiteral("Not enough storage on the phone"));
    CHECK(reports.back() == QStringLiteral("bad ") + AppCatalog::tr("Could not clear the data of %1: %2")
                                                         .arg(QStringLiteral("C"),
                                                              QStringLiteral("Not enough storage on the phone")));

    REQUIRE(catalog.run(QStringLiteral("forceStop"), {QStringLiteral("com.d")}) == 1);
    catalog.actionFinished(jobAt(phone, 5), QStringLiteral("A"), QStringLiteral("com.d"), true, {});
    CHECK(reports.back() == QStringLiteral("ok ") + AppCatalog::tr("Force stopped %1").arg(QStringLiteral("D")));
    CHECK(reports.size() == 4);
    CHECK(phone.lists.size() == lists + 4);
}

TEST_CASE("opening another phone drops the choice, the selection and the work of the last one")
{
    QStringList reports;
    QTemporaryDir dir;
    FakePhone phone;
    AppCatalog catalog(pathOf(dir), phone.source());
    watch(catalog, reports);
    catalog.setActions(phone.actions());
    catalog.open(QStringLiteral("A"));
    catalog.listed(QStringLiteral("A"), phone.lists[0], {entry("com.a", "A", 1), entry("com.b", "B", 1)});
    catalog.setCurrent(QStringLiteral("com.a"));
    catalog.setSelecting(true);
    catalog.toggleChecked(QStringLiteral("com.b"));
    REQUIRE(catalog.run(QStringLiteral("forceStop"), {QStringLiteral("com.b")}) == 1);

    catalog.open(QStringLiteral("B"));
    CHECK(catalog.current().isEmpty());
    CHECK_FALSE(catalog.selecting());
    CHECK(catalog.checkedCount() == 0);
    CHECK_FALSE(catalog.busy());
    catalog.actionFinished(jobAt(phone, 0), QStringLiteral("A"), QStringLiteral("com.b"), true, {});
    CHECK(reports.isEmpty());
}

TEST_CASE("an app disabled here stays listed so that it can be enabled again")
{
    QTemporaryDir dir;
    FakePhone phone;
    AppCatalog catalog(pathOf(dir), phone.source());
    catalog.setActions(phone.actions());
    catalog.open(QStringLiteral("A"));
    catalog.listed(QStringLiteral("A"), phone.lists[0], {entry("com.a", "A", 1), entry("com.b", "B", 1)});
    catalog.setCurrent(QStringLiteral("com.a"));

    REQUIRE(catalog.run(QStringLiteral("disable"), {QStringLiteral("com.a")}) == 1);
    catalog.actionFinished(jobAt(phone, 0), QStringLiteral("A"), QStringLiteral("com.a"), true, {});
    catalog.listed(QStringLiteral("A"), phone.lists.back(), {entry("com.b", "B", 1)});
    const AppItem* off = catalog.model().find(QStringLiteral("com.a"));
    REQUIRE(off != nullptr);
    CHECK_FALSE(off->enabled);
    CHECK(catalog.total() == 2);
    CHECK_FALSE(catalog.apps()->data(catalog.apps()->index(0, 0), AppListModel::EnabledRole).toBool());
    CHECK(catalog.current() == QStringLiteral("com.a"));
    CHECK(catalog.currentApp().value(QStringLiteral("enable")).toBool());
    CHECK_FALSE(catalog.currentApp().value(QStringLiteral("disable")).toBool());
    CHECK_FALSE(catalog.currentApp().value(QStringLiteral("forceStop")).toBool());
    CHECK(catalog.currentApp().value(QStringLiteral("uninstall")).toBool());
    catalog.launch(QStringLiteral("com.a"), false);
    CHECK(phone.launches.isEmpty());

    REQUIRE(catalog.run(QStringLiteral("disable"), {QStringLiteral("com.b")}) == 1);
    catalog.actionFinished(jobAt(phone, 1), QStringLiteral("A"), QStringLiteral("com.b"), true, {});
    catalog.listed(QStringLiteral("A"), phone.lists.back(), {disabled("com.b", "B")});
    CHECK(catalog.total() == 2);
    REQUIRE(catalog.model().find(QStringLiteral("com.b")) != nullptr);
    CHECK_FALSE(catalog.model().find(QStringLiteral("com.b"))->enabled);

    REQUIRE(catalog.run(QStringLiteral("enable"), {QStringLiteral("com.a")}) == 1);
    catalog.actionFinished(jobAt(phone, 2), QStringLiteral("A"), QStringLiteral("com.a"), true, {});
    catalog.listed(QStringLiteral("A"), phone.lists.back(), {entry("com.a", "A", 1)});
    REQUIRE(catalog.model().find(QStringLiteral("com.a")) != nullptr);
    CHECK(catalog.model().find(QStringLiteral("com.a"))->enabled);
    catalog.reload();
    catalog.listed(QStringLiteral("A"), phone.lists.back(), {});
    CHECK(catalog.model().find(QStringLiteral("com.a")) == nullptr);
    CHECK(catalog.model().find(QStringLiteral("com.b")) == nullptr);
    CHECK(catalog.current().isEmpty());
}

TEST_CASE("installs started on the page show their progress, report the result and refresh the list")
{
    QStringList reports;
    QTemporaryDir dir;
    FakePhone phone;
    AppCatalog catalog(pathOf(dir), phone.source());
    watch(catalog, reports);
    CHECK(catalog.install({QUrl::fromLocalFile(QStringLiteral("C:/apks/game.apk"))}) == 0);
    catalog.setActions(phone.actions());
    catalog.open(QStringLiteral("A"));
    catalog.listed(QStringLiteral("A"), phone.lists[0], {entry("com.a", "A", 1)});

    CHECK(catalog.install({QUrl::fromLocalFile(QStringLiteral("C:/apks/game.apk")),
                           QUrl::fromLocalFile(QStringLiteral("C:/apks/notes.txt")),
                           QUrl(QStringLiteral("https://example.com/remote.apk"))}) == 1);
    REQUIRE(phone.installs.size() == 1);
    CHECK(phone.installs[0].second == QStringLiteral("C:/apks/game.apk"));
    CHECK(reports == QStringList{QStringLiteral("warn ") +
                                 AppCatalog::tr("Only APK files on this computer can be installed")});
    const quint64 id = phone.installs[0].first;
    const QString name = QStringLiteral("game.apk");

    catalog.installUpdated(gs::app::installUpdate(id, QStringLiteral("A"), name, QStringLiteral("copying"), 50, 200, {}));
    CHECK(catalog.activity() == AppCatalog::tr("Copying %1 to the phone… %2%").arg(name).arg(25));
    catalog.installUpdated(
        gs::app::installUpdate(id, QStringLiteral("B"), name, QStringLiteral("installing"), 200, 200, {}));
    CHECK(catalog.activity() == AppCatalog::tr("Copying %1 to the phone… %2%").arg(name).arg(25));
    catalog.installUpdated(
        gs::app::installUpdate(id, QStringLiteral("A"), name, QStringLiteral("installing"), 200, 200, {}));
    CHECK(catalog.activity() == AppCatalog::tr("Installing %1…").arg(name));
    const std::size_t lists = phone.lists.size();
    catalog.installUpdated(gs::app::installUpdate(id, QStringLiteral("A"), name, QStringLiteral("done"), 200, 200, {}));
    CHECK(catalog.activity().isEmpty());
    CHECK(reports.back() == QStringLiteral("ok ") + AppCatalog::tr("Installed %1").arg(name));
    CHECK(phone.lists.size() == lists + 1);

    catalog.installUpdated(gs::app::installUpdate(id + 50, QStringLiteral("A"), QStringLiteral("other.apk"),
                                                  QStringLiteral("done"), 1, 1, {}));
    CHECK(reports.size() == 2);
    CHECK(phone.lists.size() == lists + 2);

    REQUIRE(catalog.install({QUrl::fromLocalFile(QStringLiteral("C:/apks/game.apk"))}) == 1);
    catalog.installUpdated(gs::app::installUpdate(phone.installs.back().first, QStringLiteral("A"), name,
                                                  QStringLiteral("failed"), 200, 200, QStringLiteral("Not a valid APK")));
    CHECK(reports.back() == QStringLiteral("bad ") + AppCatalog::tr("Could not install %1: %2")
                                                         .arg(name, QStringLiteral("Not a valid APK")));
    CHECK(phone.lists.size() == lists + 2);

    phone.availability.connected = false;
    catalog.availabilityChanged(QStringLiteral("A"));
    CHECK(catalog.install({QUrl::fromLocalFile(QStringLiteral("C:/apks/game.apk"))}) == 0);
    CHECK(phone.installs.size() == 2);
}
