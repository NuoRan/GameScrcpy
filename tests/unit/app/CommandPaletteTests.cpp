#include "CommandMatcher.h"
#include "CommandModel.h"
#include "CommandTable.h"
#include "Commands.h"
#include "DeviceCommands.h"

#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QVariant>
#include <QVariantMap>

#include <doctest/doctest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

using gs::app::Command;
using gs::app::CommandMatch;
using gs::app::CommandMatcher;
using gs::app::CommandModel;
using gs::app::Commands;
using gs::app::CommandText;
using gs::app::DeviceFacts;
using gs::app::TextRange;

namespace {

const QString kChinesePort = QStringLiteral(u"端口");

class FakeHost final : public gs::app::DeviceCommandHost
{
public:
    std::vector<DeviceFacts> list;
    QStringList calls;

    DeviceFacts& add(const QString& serial)
    {
        DeviceFacts facts;
        facts.serial = serial;
        facts.name = serial + QStringLiteral(" phone");
        list.push_back(facts);
        return list.back();
    }

    [[nodiscard]] std::vector<DeviceFacts> devices() const override { return list; }
    [[nodiscard]] std::optional<DeviceFacts> device(const QString& serial) const override
    {
        const auto found = std::ranges::find(list, serial, &DeviceFacts::serial);
        return found == list.end() ? std::nullopt : std::optional(*found);
    }
    [[nodiscard]] bool canRetryAdb() const override { return false; }

    void retryAdb() override {}
    void connectDevice(const QString& serial) override
    {
        calls.append(QStringLiteral("connect ") + serial);
    }
    void disconnectDevice(const QString& serial) override
    {
        calls.append(QStringLiteral("disconnect ") + serial);
    }
    void showWindow(const QString&) override {}
    void setTransport(const QString& serial, int transport) override
    {
        calls.append(QStringLiteral("transport %1 %2").arg(serial).arg(transport));
    }
    void setMuted(const QString& serial, bool muted) override
    {
        calls.append(QStringLiteral("mute %1 %2").arg(serial).arg(muted ? 1 : 0));
    }
    void setVolume(const QString&, double) override {}
    void setScreenOff(const QString&, bool) override {}
    void setOrientation(const QString& serial, int orientation) override
    {
        calls.append(QStringLiteral("orientation %1 %2").arg(serial).arg(orientation));
    }
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

CommandText titled(const QString& title)
{
    return {.title = title, .sourceTitle = title, .id = QStringLiteral("test.command")};
}

CommandText hidden(CommandText text)
{
    text.title = kChinesePort;
    return text;
}

int scoreOf(QStringView query, const CommandText& text)
{
    return CommandMatcher(query).match(text).score;
}

std::string spans(const std::vector<TextRange>& ranges)
{
    std::string text;
    for (const TextRange& range : ranges) {
        text += text.empty() ? "" : " ";
        text += std::to_string(range.start) + '+' + std::to_string(range.length);
    }
    return text;
}

std::string spans(const QVariant& highlights)
{
    std::vector<TextRange> ranges;
    for (const QVariant& range : highlights.toList()) {
        const QVariantList pair = range.toList();
        ranges.push_back({pair.value(0).toInt(), pair.value(1).toInt()});
    }
    return spans(ranges);
}

QStringList column(const CommandModel& model, int role)
{
    QStringList values;
    for (int row = 0; row < model.rowCount(); ++row) {
        values.append(model.data(model.index(row), role).toString());
    }
    return values;
}

std::string ids(const CommandModel& model)
{
    return column(model, CommandModel::CommandIdRole).join(u' ').toStdString();
}

int rowOf(const CommandModel& model, const QString& id)
{
    return static_cast<int>(column(model, CommandModel::CommandIdRole).indexOf(id));
}

QVariant cell(const CommandModel& model, const QString& id, int role)
{
    return model.data(model.index(rowOf(model, id)), role);
}

}

TEST_CASE("match tiers: title word start, substring, subsequence, other titles, keyword, id")
{
    const CommandText group{.title = kChinesePort,
                            .groupTitles = {kChinesePort, QStringLiteral("Ports")}};
    const CommandText keywords{.title = kChinesePort, .keywords = {QStringLiteral("usb port")}};
    const CommandText device{.title = kChinesePort, .id = QStringLiteral("device.port")};
    const CommandText other{.title = kChinesePort, .id = QStringLiteral("test.other")};

    const int wordStart = scoreOf(u"port", titled(QStringLiteral("Port forwarding")));
    const int lateWordStart = scoreOf(u"port", titled(QStringLiteral("Forward the local port")));
    const int substring = scoreOf(u"port", titled(QStringLiteral("Xport")));
    const int subsequence = scoreOf(u"port", titled(QStringLiteral("Pick a sort")));
    const int sourceTitle = scoreOf(u"port", hidden(titled(QStringLiteral("Port forwarding"))));
    const int groupTitle = scoreOf(u"port", group);
    const int keyword = scoreOf(u"port", keywords);
    const int id = scoreOf(u"port", device);

    CHECK(wordStart > lateWordStart);
    CHECK(lateWordStart > substring);
    CHECK(substring > subsequence);
    CHECK(subsequence > sourceTitle);
    CHECK(sourceTitle == groupTitle);
    CHECK(groupTitle > keyword);
    CHECK(keyword > id);
    CHECK(id > 0);
    CHECK(scoreOf(u"port", other) == 0);
}

TEST_CASE("a subsequence scores less as its gaps grow, and prefers the starts of words")
{
    const CommandMatcher matcher(u"scrof");
    const CommandMatch tight = matcher.match(titled(QStringLiteral("Turn the screen off")));
    const CommandMatch loose = matcher.match(titled(QStringLiteral("Turn the screen back off")));
    REQUIRE(tight.matched);
    REQUIRE(loose.matched);
    CHECK(tight.score > loose.score);
    CHECK(spans(tight.highlights) == "9+3 16+2");
    CHECK(spans(loose.highlights) == "9+3 21+2");

    const CommandMatch volumeUp = CommandMatcher(u"vu").match(titled(QStringLiteral("Volume up")));
    CHECK(spans(volumeUp.highlights) == "0+1 7+1");
}

TEST_CASE("every word of the query must be found, and the words' scores add up")
{
    const CommandText screenOff = titled(QStringLiteral("Turn the screen off"));
    const CommandMatch both = CommandMatcher(u"screen off").match(screenOff);
    REQUIRE(both.matched);
    CHECK(both.score == scoreOf(u"screen", screenOff) + scoreOf(u"off", screenOff));
    CHECK(spans(both.highlights) == "9+6 16+3");

    const CommandMatch partly = CommandMatcher(u"screen zzz").match(screenOff);
    CHECK_FALSE(partly.matched);
    CHECK(partly.score == 0);
    CHECK(partly.highlights.empty());
    CHECK_FALSE(CommandMatcher(u"zzz").match(screenOff).matched);
}

TEST_CASE("case does not matter, and an ideographic space separates words")
{
    const CommandText screenOff = titled(QStringLiteral("Turn the screen off"));
    const CommandMatch plain = CommandMatcher(u"screen off").match(screenOff);
    const CommandMatch shouted = CommandMatcher(u" SCREEN\u3000Off  ").match(screenOff);
    REQUIRE(shouted.matched);
    CHECK(shouted.score == plain.score);
    CHECK(spans(shouted.highlights) == spans(plain.highlights));
    CHECK(scoreOf(u"ＳＣＲＥＥＮ", screenOff) == scoreOf(u"screen", screenOff));
}

TEST_CASE("highlights mark only the shown title, merged where the words touch or overlap")
{
    const CommandText screenOff = titled(QStringLiteral("Turn the screen off"));
    CHECK(spans(CommandMatcher(u"scr een").match(screenOff).highlights) == "9+6");
    CHECK(spans(CommandMatcher(u"scre reen").match(screenOff).highlights) == "9+6");
    CHECK(spans(CommandMatcher(u"off screen").match(screenOff).highlights) == "9+6 16+3");

    const CommandMatch english = CommandMatcher(u"turn").match(hidden(screenOff));
    CHECK(english.matched);
    CHECK(english.highlights.empty());
}

TEST_CASE("every Chinese character starts a word; Chinese matches as substrings and subsequences")
{
    const CommandText portrait = titled(QStringLiteral(u"保持竖屏"));
    const CommandMatch inside = CommandMatcher(u"竖屏").match(portrait);
    REQUIRE(inside.matched);
    CHECK(spans(inside.highlights) == "2+2");
    CHECK(inside.score == scoreOf(u"ab", titled(QStringLiteral("x ab"))));
    CHECK(inside.score > scoreOf(u"ab", titled(QStringLiteral("xxab"))));

    const CommandMatch spread = CommandMatcher(u"保竖").match(portrait);
    REQUIRE(spread.matched);
    CHECK(spans(spread.highlights) == "0+1 2+1");
    CHECK(spread.score < inside.score);
    CHECK_FALSE(CommandMatcher(u"横屏").match(portrait).matched);

    const CommandText wifi = titled(QStringLiteral(u"经WiFi直连"));
    CHECK(spans(CommandMatcher(u"wifi").match(wifi).highlights) == "1+4");
    CHECK(scoreOf(u"wifi", wifi) == scoreOf(u"wifi", titled(QStringLiteral("-WiFi"))));
    CHECK(scoreOf(u"直连 wifi", wifi) > scoreOf(u"直连", wifi));
}

TEST_CASE("keywords and ids prefer the starts of words, camel humps included")
{
    const CommandText leading{.title = kChinesePort, .keywords = {QStringLiteral("less noise")}};
    const CommandText trailing{.title = kChinesePort, .keywords = {QStringLiteral("wireless")}};
    CHECK(scoreOf(u"less", leading) > scoreOf(u"less", trailing));
    CHECK(scoreOf(u"less", trailing) > 0);

    const CommandText hump{.title = kChinesePort, .id = QStringLiteral("device.appSwitch")};
    const CommandText flat{.title = kChinesePort, .id = QStringLiteral("device.appswitch")};
    CHECK(scoreOf(u"switch", hump) > scoreOf(u"switch", flat));
}

TEST_CASE("ties go to the shorter title, then to the id")
{
    const CommandMatcher matcher(u"mu");
    const CommandText mute{.title = QStringLiteral("Mute"), .id = QStringLiteral("device.mute")};
    const CommandText muteAll{.title = QStringLiteral("Mute all"),
                              .id = QStringLiteral("app.muteAll")};
    const CommandText mutt{.title = QStringLiteral("Mutt"), .id = QStringLiteral("app.mutt")};
    const CommandText unmute{.title = QStringLiteral("Unmute"),
                             .id = QStringLiteral("device.unmute")};
    const CommandMatch muteMatch = matcher.match(mute);
    const CommandMatch muteAllMatch = matcher.match(muteAll);
    const CommandMatch muttMatch = matcher.match(mutt);
    REQUIRE(muteMatch.score == muteAllMatch.score);
    REQUIRE(muteMatch.score == muttMatch.score);

    CHECK(CommandMatcher::ranksBefore(muteMatch, mute, muteAllMatch, muteAll));
    CHECK_FALSE(CommandMatcher::ranksBefore(muteAllMatch, muteAll, muteMatch, mute));
    CHECK(CommandMatcher::ranksBefore(muttMatch, mutt, muteMatch, mute));
    CHECK(CommandMatcher::ranksBefore(muteAllMatch, muteAll, matcher.match(unmute), unmute));
}

TEST_CASE("an empty query matches everything, without a score or highlights")
{
    const CommandMatcher matcher(u" \u3000 ");
    CHECK(matcher.isEmpty());
    const CommandMatch match = matcher.match(titled(QStringLiteral("Mute")));
    CHECK(match.matched);
    CHECK(match.score == 0);
    CHECK(match.highlights.empty());
}

TEST_CASE("a group matches by its own title or any variant's, and highlights only its own title")
{
    const QStringList groupTitles{QStringLiteral("Orientation")};
    const std::vector<CommandText> orientation{
        {.title = QStringLiteral("Orientation"),
         .sourceTitle = QStringLiteral("Orientation"),
         .id = QStringLiteral("device.orientation")},
        {.title = QStringLiteral("Keep portrait"),
         .sourceTitle = QStringLiteral("Keep portrait"),
         .id = QStringLiteral("device.orientation.portrait"),
         .groupTitles = groupTitles},
        {.title = QStringLiteral("Keep landscape"),
         .sourceTitle = QStringLiteral("Keep landscape"),
         .id = QStringLiteral("device.orientation.landscape"),
         .groupTitles = groupTitles},
    };
    const CommandMatch landscape = CommandMatcher(u"landscape").match(orientation);
    REQUIRE(landscape.matched);
    CHECK(landscape.score == scoreOf(u"landscape", orientation[2]));
    CHECK(landscape.highlights.empty());

    const CommandMatch both = CommandMatcher(u"orient keep").match(orientation);
    REQUIRE(both.matched);
    CHECK(spans(both.highlights) == "0+6");
    CHECK_FALSE(CommandMatcher(u"orient zzz").match(orientation).matched);
}

TEST_CASE("the variants of a group share one group title")
{
    FakeHost host;
    const Commands commands(host);
    QHash<QString, QString> titles;
    for (const Command& command : commands.table().all()) {
        if (command.group.isEmpty()) {
            continue;
        }
        REQUIRE_MESSAGE(command.groupTitle != nullptr, command.id.toStdString());
        const QString title = QString::fromUtf8(command.groupTitle);
        CHECK_MESSAGE(titles.value(command.group, title) == title, command.id.toStdString());
        titles.insert(command.group, title);
    }
    CHECK(titles.value(QStringLiteral("device.transport")) == QStringLiteral("Connection method"));
    CHECK(titles.value(QStringLiteral("device.orientation")) == QStringLiteral("Orientation"));
}

TEST_CASE("the palette lists the commands available for its target, in the table's order")
{
    FakeHost host;
    host.add(QStringLiteral("A")).canConnect = true;
    Commands commands(host);
    CommandModel model;
    model.setTarget(QStringLiteral("A"));

    const QStringList listed = column(model, CommandModel::CommandIdRole);
    const auto at = [&listed](const char* id) {
        return listed.indexOf(QString::fromLatin1(id));
    };
    CHECK(model.count() == listed.size());
    CHECK(at("device.mirror") >= 0);
    CHECK(at("hub.settings") >= 0);
    CHECK(at("device.disconnect") < 0);
    CHECK(at("device.mute") < 0);
    CHECK(at("adb.retry") < 0);
    CHECK(at("device.mirror") < at("device.transport"));
    CHECK(at("device.transport") < at("hub.devices"));
    CHECK(at("hub.devices") < at("app.about"));

    const QString mirror = QStringLiteral("device.mirror");
    CHECK(cell(model, mirror, CommandModel::KindRole).toString() == QStringLiteral("command"));
    CHECK(cell(model, mirror, CommandModel::TitleRole).toString() == QStringLiteral("Mirror"));
    CHECK(cell(model, mirror, CommandModel::IconRole).toString() == QStringLiteral("monitor-play"));
    CHECK(cell(model, mirror, CommandModel::TargetNameRole).toString() ==
          QStringLiteral("A phone"));
    CHECK_FALSE(cell(model, mirror, CommandModel::CheckedRole).toBool());
    CHECK(cell(model, mirror, CommandModel::HighlightsRole).toList().isEmpty());
    const QString settings = QStringLiteral("hub.settings");
    CHECK(cell(model, settings, CommandModel::TargetNameRole).toString().isEmpty());
    const QString search = QStringLiteral("settings.search");
    CHECK_FALSE(commands.shortcutText(search).isEmpty());
    CHECK(cell(model, search, CommandModel::ShortcutRole).toStringList() == commands.shortcutCaps(search));
    CHECK(model.sectionCount() == 0);
    CHECK(column(model, CommandModel::SectionRole).join(QString()).isEmpty());
}

TEST_CASE("a group's variants make one row named by the group, the variant in effect as subtitle")
{
    FakeHost host;
    DeviceFacts& phone = host.add(QStringLiteral("A"));
    phone.canConnect = true;
    phone.transport = 1;
    Commands commands(host);
    CommandModel model;
    model.setTarget(QStringLiteral("A"));

    const QStringList listed = column(model, CommandModel::CommandIdRole);
    CHECK(listed.count(QStringLiteral("device.transport")) == 1);
    CHECK_FALSE(listed.contains(QStringLiteral("device.transport.wifi")));
    CHECK_FALSE(listed.contains(QStringLiteral("device.orientation")));

    const QString transport = QStringLiteral("device.transport");
    CHECK(cell(model, transport, CommandModel::KindRole).toString() == QStringLiteral("group"));
    CHECK(cell(model, transport, CommandModel::TitleRole).toString() ==
          QStringLiteral("Connection method"));
    CHECK(cell(model, transport, CommandModel::SubtitleRole).toString() ==
          QStringLiteral("Connect over WiFi direct"));
    CHECK(cell(model, transport, CommandModel::TargetNameRole).toString() ==
          QStringLiteral("A phone"));
    CHECK_FALSE(cell(model, transport, CommandModel::CheckedRole).toBool());

    const QVariantList choices = model.choices(rowOf(model, transport));
    REQUIRE(choices.size() == 3);
    QStringList variants;
    QStringList checked;
    for (const QVariant& choice : choices) {
        const QVariantMap variant = choice.toMap();
        variants.append(variant.value(QStringLiteral("commandId")).toString());
        if (variant.value(QStringLiteral("checked")).toBool()) {
            checked.append(variant.value(QStringLiteral("title")).toString());
        }
        CHECK(variant.value(QStringLiteral("enabled")).toBool());
    }
    CHECK(variants.join(u' ').toStdString() ==
          "device.transport.adb device.transport.wifi device.transport.auto");
    CHECK(checked == QStringList{QStringLiteral("Connect over WiFi direct")});
    CHECK(model.choices(rowOf(model, QStringLiteral("device.mirror"))).isEmpty());
    CHECK(model.choices(-1).isEmpty());

    host.list.front().rotationControl = true;
    host.list.front().orientation = 2;
    commands.refresh();
    const QString orientation = QStringLiteral("device.orientation");
    REQUIRE(rowOf(model, orientation) >= 0);
    CHECK(cell(model, orientation, CommandModel::TitleRole).toString() ==
          QStringLiteral("Orientation"));
    CHECK(cell(model, orientation, CommandModel::SubtitleRole).toString() ==
          QStringLiteral("Keep landscape"));
}

TEST_CASE("a query filters and orders the rows and marks what matched in each title")
{
    FakeHost host;
    DeviceFacts& phone = host.add(QStringLiteral("B"));
    phone.connected = true;
    phone.audio = true;
    phone.rotationControl = true;
    phone.deviceId = QStringLiteral("aid:1");
    Commands commands(host);
    CommandModel model;
    model.setTarget(QStringLiteral("B"));

    model.setQuery(QStringLiteral("mute"));
    CHECK(ids(model) == "device.mute");
    CHECK(spans(model.data(model.index(0), CommandModel::HighlightsRole)) == "0+4");

    model.setQuery(QStringLiteral("set"));
    CHECK(ids(model) == "hub.settings device.settings settings.search device.toolbar device.disconnect");
    CHECK(spans(model.data(model.index(0), CommandModel::HighlightsRole)) == "5+3");
    CHECK(spans(model.data(model.index(3), CommandModel::HighlightsRole)) == "0+1 11+1 13+1");
    CHECK(spans(model.data(model.index(4), CommandModel::HighlightsRole)) == "2+1 7+1 9+1");

    model.setQuery(QStringLiteral("landscape"));
    CHECK(ids(model) == "device.orientation");
    CHECK(model.data(model.index(0), CommandModel::HighlightsRole).toList().isEmpty());
    model.setQuery(QStringLiteral("orient"));
    CHECK(ids(model) == "device.orientation");
    CHECK(spans(model.data(model.index(0), CommandModel::HighlightsRole)) == "0+6");

    model.setQuery(QStringLiteral("zzz"));
    CHECK(model.count() == 0);
    model.setQuery(QString());
    CHECK(model.count() > 0);
    CHECK(model.data(model.index(0), CommandModel::HighlightsRole).toList().isEmpty());
}

TEST_CASE("the target picks the device the listed commands act on and name")
{
    FakeHost host;
    host.add(QStringLiteral("A")).canConnect = true;
    host.add(QStringLiteral("B")).connected = true;
    Commands commands(host);
    CommandModel model;
    int targets = 0;
    QObject::connect(&model, &CommandModel::targetChanged, &model, [&targets] { ++targets; });

    const QString mirror = QStringLiteral("device.mirror");
    const QString disconnect = QStringLiteral("device.disconnect");
    model.setTarget(QStringLiteral("A"));
    QStringList listed = column(model, CommandModel::CommandIdRole);
    CHECK(listed.contains(mirror));
    CHECK_FALSE(listed.contains(disconnect));
    CHECK(cell(model, mirror, CommandModel::TargetNameRole).toString() ==
          QStringLiteral("A phone"));

    model.setTarget(QStringLiteral("B"));
    listed = column(model, CommandModel::CommandIdRole);
    CHECK_FALSE(listed.contains(mirror));
    CHECK(listed.contains(disconnect));
    CHECK(cell(model, disconnect, CommandModel::TargetNameRole).toString() ==
          QStringLiteral("B phone"));

    model.setTarget(QString());
    CHECK(column(model, CommandModel::CommandIdRole) == listed);
    CHECK(targets == 3);

    host.list[1].connected = false;
    commands.refresh();
    listed = column(model, CommandModel::CommandIdRole);
    CHECK_FALSE(listed.contains(mirror));
    CHECK_FALSE(listed.contains(disconnect));
    CHECK(listed.contains(QStringLiteral("hub.devices")));
}

TEST_CASE("triggering a row runs its command on its target, and a group row runs nothing")
{
    FakeHost host;
    host.add(QStringLiteral("A")).canConnect = true;
    host.add(QStringLiteral("B")).connected = true;
    host.list[1].audio = true;
    Commands commands(host);
    CommandModel model;

    model.setTarget(QStringLiteral("B"));
    CHECK(model.trigger(rowOf(model, QStringLiteral("device.mute"))));
    model.setTarget(QStringLiteral("A"));
    CHECK(model.trigger(rowOf(model, QStringLiteral("device.mirror"))));
    CHECK_FALSE(model.trigger(rowOf(model, QStringLiteral("device.transport"))));
    CHECK_FALSE(model.trigger(-1));
    CHECK_FALSE(model.trigger(model.count()));
    CHECK(host.calls == QStringList{QStringLiteral("mute B 1"), QStringLiteral("connect A")});
    CHECK(commands.currentDevice() == QStringLiteral("A"));
}

TEST_CASE("the list follows the command state, resetting only when its rows change")
{
    FakeHost host;
    host.add(QStringLiteral("B")).connected = true;
    Commands commands(host);
    CommandModel model;
    model.setTarget(QStringLiteral("B"));
    int resets = 0;
    int changes = 0;
    int counts = 0;
    QObject::connect(&model, &QAbstractItemModel::modelReset, &model, [&resets] { ++resets; });
    QObject::connect(&model, &QAbstractItemModel::dataChanged, &model, [&changes] { ++changes; });
    QObject::connect(&model, &CommandModel::countChanged, &model, [&counts] { ++counts; });

    commands.refresh();
    CHECK(resets == 0);
    CHECK(changes == 0);

    host.list.front().diagnostics = true;
    commands.refresh();
    CHECK(resets == 0);
    CHECK(changes == 1);
    CHECK(cell(model, QStringLiteral("device.diagnostics"), CommandModel::CheckedRole).toBool());

    const int before = model.count();
    host.list.front().audio = true;
    commands.refresh();
    CHECK(resets == 1);
    CHECK(counts == 1);
    CHECK(model.count() == before + 3);
    CHECK(rowOf(model, QStringLiteral("device.mute")) >= 0);
}

TEST_CASE("with an empty query the commands run from the palette come first, newest first, in their own section")
{
    FakeHost host;
    DeviceFacts& phone = host.add(QStringLiteral("A"));
    phone.canConnect = true;
    phone.transport = 1;
    Commands commands(host);
    std::vector<std::string> stored;
    commands.setRecentStore([&stored](const std::vector<std::string>& ids) { stored = ids; });
    CommandModel model;
    model.setTarget(QStringLiteral("A"));
    int sectionChanges = 0;
    QObject::connect(&model, &CommandModel::sectionCountChanged, &model, [&sectionChanges] { ++sectionChanges; });

    CHECK(model.trigger(rowOf(model, QStringLiteral("hub.settings"))));
    CHECK(model.triggerChoice(QStringLiteral("device.transport"), QStringLiteral("device.transport.adb")));
    CHECK_FALSE(model.triggerChoice(QStringLiteral("device.transport"), QStringLiteral("device.mirror")));
    CHECK_FALSE(model.triggerChoice(QStringLiteral("device.mirror"), QStringLiteral("device.mirror")));
    CHECK(stored == std::vector<std::string>{"device.transport", "hub.settings"});
    CHECK(host.calls == QStringList{QStringLiteral("transport A 0")});

    const QStringList listed = column(model, CommandModel::CommandIdRole);
    CHECK(listed.mid(0, 2) == QStringList{QStringLiteral("device.transport"), QStringLiteral("hub.settings")});
    const QStringList sections = column(model, CommandModel::SectionRole);
    CHECK(sections.mid(0, 2) == QStringList{QStringLiteral("recent"), QStringLiteral("recent")});
    CHECK(sections.mid(2).count(QStringLiteral("other")) == sections.size() - 2);
    CHECK(model.sectionCount() == 2);
    CHECK(sectionChanges == 1);

    model.setQuery(QStringLiteral("settings"));
    CHECK(column(model, CommandModel::SectionRole).join(QString()).isEmpty());
    CHECK(model.sectionCount() == 0);
}

TEST_CASE("recent commands keep eight distinct ids, the newest first")
{
    FakeHost host;
    Commands commands(host);
    int changes = 0;
    QObject::connect(&commands, &Commands::recentChanged, &commands, [&changes] { ++changes; });
    commands.applyRecentCommands({"hub.devices", "hub.devices", "", "app.about"});
    CHECK(commands.recentCommands() == QStringList{QStringLiteral("hub.devices"), QStringLiteral("app.about")});
    for (int i = 0; i < 10; ++i) {
        commands.remember(QStringLiteral("test.c%1").arg(i));
    }
    CHECK(commands.recentCommands().size() == 8);
    CHECK(commands.recentCommands().front() == QStringLiteral("test.c9"));
    commands.remember(QStringLiteral("test.c5"));
    commands.remember(QStringLiteral("test.c5"));
    CHECK(commands.recentCommands().front() == QStringLiteral("test.c5"));
    CHECK(commands.recentCommands().count(QStringLiteral("test.c5")) == 1);
    CHECK(changes == 12);
}

TEST_CASE("a hidden palette list catches up with the command state when it is shown")
{
    FakeHost host;
    host.add(QStringLiteral("B")).connected = true;
    Commands commands(host);
    CommandModel model;
    model.setTarget(QStringLiteral("B"));
    model.setActive(false);
    int resets = 0;
    QObject::connect(&model, &QAbstractItemModel::modelReset, &model, [&resets] { ++resets; });

    host.list.front().audio = true;
    commands.refresh();
    CHECK(resets == 0);
    CHECK(rowOf(model, QStringLiteral("device.mute")) < 0);
    model.setActive(true);
    CHECK(resets == 1);
    CHECK(rowOf(model, QStringLiteral("device.mute")) >= 0);
}

TEST_CASE("without the command table the palette is empty")
{
    CommandModel model;
    model.setQuery(QStringLiteral("mute"));
    CHECK(model.count() == 0);
    CHECK(model.rowCount() == 0);
    CHECK_FALSE(model.trigger(0));
    CHECK(model.choices(0).isEmpty());
}
