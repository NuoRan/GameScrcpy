#include "CommandTable.h"
#include "Commands.h"
#include "DeviceCommands.h"
#include "Esp32Boards.h"
#include "SettingsCatalog.h"
#include "ShortcutModel.h"

#include <doctest/doctest.h>

#include <QFile>
#include <QKeyEvent>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <iterator>
#include <optional>
#include <vector>

using gs::app::Command;
using gs::app::Commands;
using gs::app::CommandScope;
using gs::app::CommandTable;
using gs::app::DeviceFacts;
using gs::app::ShortcutMap;

namespace {

class FakeHost final : public gs::app::DeviceCommandHost
{
public:
    std::vector<DeviceFacts> list;
    bool retry = false;
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
    [[nodiscard]] bool canRetryAdb() const override { return retry; }

    void retryAdb() override { calls.append(QStringLiteral("retry")); }
    void connectDevice(const QString& serial) override { calls.append(QStringLiteral("connect ") + serial); }
    void disconnectDevice(const QString& serial) override { calls.append(QStringLiteral("disconnect ") + serial); }
    void showWindow(const QString& serial) override { calls.append(QStringLiteral("show ") + serial); }
    void setTransport(const QString& serial, int transport) override
    {
        calls.append(QStringLiteral("transport %1 %2").arg(serial).arg(transport));
    }
    void setMuted(const QString& serial, bool muted) override
    {
        calls.append(QStringLiteral("mute %1 %2").arg(serial).arg(muted ? 1 : 0));
    }
    void setVolume(const QString& serial, double volume) override
    {
        calls.append(QStringLiteral("volume %1 %2").arg(serial).arg(volume, 0, 'f', 1));
    }
    void setScreenOff(const QString& serial, bool off) override
    {
        calls.append(QStringLiteral("screen %1 %2").arg(serial).arg(off ? 1 : 0));
    }
    void setOrientation(const QString& serial, int orientation) override
    {
        calls.append(QStringLiteral("orientation %1 %2").arg(serial).arg(orientation));
    }
    void pressKey(const QString& serial, int keycode) override
    {
        calls.append(QStringLiteral("key %1 %2").arg(serial).arg(keycode));
    }
    void setDiagnostics(const QString& serial, bool shown) override
    {
        calls.append(QStringLiteral("diagnostics %1 %2").arg(serial).arg(shown ? 1 : 0));
    }
    void setFullScreen(const QString& serial, bool on) override
    {
        calls.append(QStringLiteral("fullscreen %1 %2").arg(serial).arg(on ? 1 : 0));
    }
    void setMini(const QString& serial, bool on) override
    {
        calls.append(QStringLiteral("mini %1 %2").arg(serial).arg(on ? 1 : 0));
    }
    void setEditing(const QString& serial, bool on) override
    {
        calls.append(QStringLiteral("editing %1 %2").arg(serial).arg(on ? 1 : 0));
    }
    void editorAction(const QString& serial, const QString& action) override
    {
        calls.append(QStringLiteral("editor %1 %2").arg(serial, action));
    }
    void setMeasuringLatency(const QString& serial, bool on) override
    {
        calls.append(QStringLiteral("latency %1 %2").arg(serial).arg(on ? 1 : 0));
    }
    void toggleToolbar(const QString& serial) override
    {
        calls.append(QStringLiteral("toolbar %1").arg(serial));
    }
    void setKeymap(const QString& serial, bool on) override
    {
        calls.append(QStringLiteral("keymap %1 %2").arg(serial).arg(on ? 1 : 0));
    }
    void setCameraLocked(const QString& serial, bool locked) override
    {
        calls.append(QStringLiteral("camera %1 %2").arg(serial).arg(locked ? 1 : 0));
    }
    void setKeyHints(const QString& serial, bool shown) override
    {
        calls.append(QStringLiteral("hints %1 %2").arg(serial).arg(shown ? 1 : 0));
    }
    void stepProfile(const QString& serial, int step) override
    {
        calls.append(QStringLiteral("profile %1 %2").arg(serial).arg(step));
    }
    void endWireless(const QString& serial) override { calls.append(QStringLiteral("end ") + serial); }
    void showSystemPanel(const QString& serial, gs::protocol::SystemPanelAction action) override
    {
        calls.append(QStringLiteral("panel %1 %2").arg(serial).arg(static_cast<int>(action)));
    }
};

}

TEST_CASE("command ids are dotted lower camel case words")
{
    CHECK(CommandTable::validId(u"device.mute"));
    CHECK(CommandTable::validId(u"device.orientation.follow"));
    CHECK(CommandTable::validId(u"device.appSwitch"));
    CHECK(CommandTable::validId(u"adb.retry"));
    CHECK_FALSE(CommandTable::validId(u""));
    CHECK_FALSE(CommandTable::validId(u"device"));
    CHECK_FALSE(CommandTable::validId(u"Device.mute"));
    CHECK_FALSE(CommandTable::validId(u"device..mute"));
    CHECK_FALSE(CommandTable::validId(u"device."));
    CHECK_FALSE(CommandTable::validId(u".device"));
    CHECK_FALSE(CommandTable::validId(u"device.mute-1"));
    CHECK_FALSE(CommandTable::validId(u"device.1x"));
    CHECK(CommandTable::validId(QStringLiteral("device.") + QString(57, u'a')));
    CHECK_FALSE(CommandTable::validId(QStringLiteral("device.") + QString(58, u'a')));
}

TEST_CASE("a command table takes each id once, and only with an action")
{
    CommandTable table;
    Command command;
    command.id = QStringLiteral("test.one");
    command.run = [](const QString&) {};
    CHECK(table.add(command));
    CHECK_FALSE(table.add(command));
    Command nameless = command;
    nameless.id = QStringLiteral("Test");
    CHECK_FALSE(table.add(nameless));
    Command idle;
    idle.id = QStringLiteral("test.two");
    CHECK_FALSE(table.add(idle));
    REQUIRE(table.find(u"test.one") != nullptr);
    CHECK(table.find(u"test.two") == nullptr);
    CHECK(table.all().size() == 1);
    REQUIRE(table.find(u"test.one")->enabled);
    CHECK(table.find(u"test.one")->enabled(QString()));
}

TEST_CASE("the registered command ids match the snapshot")
{
    FakeHost host;
    const Commands commands(host);
    QStringList ids;
    for (const Command& command : commands.table().all()) {
        ids.append(command.id);
        CHECK_MESSAGE(command.title[0] != '\0', command.id.toStdString());
        CHECK(command.enabled != nullptr);
    }
    std::ranges::sort(ids);

    QFile file(QStringLiteral(GS_APP_TEST_DATA_DIR "/command-ids.txt"));
    REQUIRE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QString text = QString::fromUtf8(file.readAll());
    if (text.startsWith(QChar(0xFEFF))) {
        text.remove(0, 1);
    }
    QStringList expected;
    for (const QString& line : text.split(u'\n')) {
        if (!line.trimmed().isEmpty()) {
            expected.append(line.trimmed());
        }
    }
    std::ranges::sort(expected);
    QStringList unlisted;
    QStringList unregistered;
    std::ranges::copy_if(ids, std::back_inserter(unlisted),
                         [&expected](const QString& id) { return !expected.contains(id); });
    std::ranges::copy_if(expected, std::back_inserter(unregistered),
                         [&ids](const QString& id) { return !ids.contains(id); });
    CHECK_MESSAGE(unlisted.isEmpty(), "registered, not in command-ids.txt: " << unlisted.join(u", ").toStdString());
    CHECK_MESSAGE(unregistered.isEmpty(), "in command-ids.txt, not registered: " << unregistered.join(u", ").toStdString());
    CHECK(ids == expected);
}

TEST_CASE("device commands are available as the device's state allows, and act on that device")
{
    FakeHost host;
    host.add(QStringLiteral("A")).canConnect = true;
    host.add(QStringLiteral("B")).connected = true;
    Commands commands(host);

    CHECK(commands.isEnabled(QStringLiteral("device.mirror"), QStringLiteral("A")));
    CHECK_FALSE(commands.isEnabled(QStringLiteral("device.mirror"), QStringLiteral("B")));
    CHECK_FALSE(commands.isEnabled(QStringLiteral("device.mirror"), QStringLiteral("missing")));
    CHECK(commands.trigger(QStringLiteral("device.mirror"), QStringLiteral("A")));
    CHECK_FALSE(commands.trigger(QStringLiteral("device.mirror"), QStringLiteral("B")));
    CHECK(commands.isEnabled(QStringLiteral("device.disconnect"), QStringLiteral("B")));
    CHECK(commands.trigger(QStringLiteral("device.back"), QStringLiteral("B")));
    CHECK(commands.trigger(QStringLiteral("device.home"), QStringLiteral("B")));
    CHECK(commands.trigger(QStringLiteral("device.appSwitch"), QStringLiteral("B")));
    CHECK_FALSE(commands.trigger(QStringLiteral("device.back"), QStringLiteral("A")));
    CHECK(host.calls == QStringList{QStringLiteral("connect A"), QStringLiteral("key B 4"), QStringLiteral("key B 3"),
                                    QStringLiteral("key B 187")});
}

TEST_CASE("toggles report their state and name what they will do")
{
    FakeHost host;
    DeviceFacts& phone = host.add(QStringLiteral("B"));
    phone.connected = true;
    phone.audio = true;
    phone.screenControl = true;
    Commands commands(host);

    CHECK(commands.title(QStringLiteral("device.mute"), QStringLiteral("B")) == QStringLiteral("Mute"));
    CHECK_FALSE(commands.isChecked(QStringLiteral("device.mute"), QStringLiteral("B")));
    CHECK(commands.trigger(QStringLiteral("device.mute"), QStringLiteral("B")));
    host.list.front().muted = true;
    CHECK(commands.isChecked(QStringLiteral("device.mute"), QStringLiteral("B")));
    CHECK(commands.title(QStringLiteral("device.mute"), QStringLiteral("B")) == QStringLiteral("Unmute"));
    CHECK(commands.trigger(QStringLiteral("device.mute"), QStringLiteral("B")));

    CHECK(commands.title(QStringLiteral("device.screenOff"), QStringLiteral("B")) ==
          QStringLiteral("Turn the screen off"));
    host.list.front().screenOff = true;
    CHECK(commands.title(QStringLiteral("device.screenOff"), QStringLiteral("B")) ==
          QStringLiteral("Turn the screen on"));
    CHECK(commands.trigger(QStringLiteral("device.screenOff"), QStringLiteral("B")));
    CHECK(host.calls ==
          QStringList{QStringLiteral("mute B 1"), QStringLiteral("mute B 0"), QStringLiteral("screen B 0")});
}

TEST_CASE("the toolbar command belongs to the mirror window, has no key until the user gives it one, and acts on its device")
{
    FakeHost host;
    host.add(QStringLiteral("A"));
    host.add(QStringLiteral("B")).connected = true;
    Commands commands(host);
    const QString id = QStringLiteral("device.toolbar");

    const Command* toolbar = commands.table().find(id);
    REQUIRE(toolbar != nullptr);
    CHECK(toolbar->scope == CommandScope::Mirror);
    CHECK(toolbar->target == gs::app::CommandTarget::Device);
    CHECK(toolbar->shortcut == QKeyCombination());
    CHECK(commands.shortcutText(id).isEmpty());
    CHECK(commands.title(id, QStringLiteral("B")) == QStringLiteral("Show or hide the toolbar"));

    CHECK_FALSE(commands.trigger(id, QStringLiteral("A")));
    CHECK(commands.trigger(id, QStringLiteral("B")));
    CHECK(host.calls == QStringList{QStringLiteral("toolbar B")});
}

TEST_CASE("full screen belongs to the mirror window, is on F11 by default and toggles for the device it acts on")
{
    FakeHost host;
    host.add(QStringLiteral("A"));
    host.add(QStringLiteral("B")).connected = true;
    Commands commands(host);
    const QString id = QStringLiteral("device.fullscreen");
    const QKeyCombination f11(Qt::Key_F11);

    const Command* fullScreen = commands.table().find(id);
    REQUIRE(fullScreen != nullptr);
    CHECK(fullScreen->scope == CommandScope::Mirror);
    CHECK(fullScreen->target == gs::app::CommandTarget::Device);
    CHECK(fullScreen->shortcut == f11);
    CHECK(commands.shortcutText(id) == QStringLiteral("F11"));
    CHECK(commands.match(f11, CommandScope::Mirror) == id);
    CHECK(commands.match(f11, CommandScope::Hub).isEmpty());

    CHECK_FALSE(commands.isEnabled(id, QStringLiteral("A")));
    CHECK_FALSE(commands.trigger(id, QStringLiteral("A")));
    CHECK(commands.isEnabled(id, QStringLiteral("B")));
    CHECK_FALSE(commands.isChecked(id, QStringLiteral("B")));
    CHECK(commands.title(id, QStringLiteral("B")) == QStringLiteral("Full screen"));
    CHECK(commands.trigger(id, QStringLiteral("B")));
    host.list.back().fullScreen = true;
    CHECK(commands.isChecked(id, QStringLiteral("B")));
    CHECK(commands.title(id, QStringLiteral("B")) == QStringLiteral("Exit full screen"));
    CHECK(commands.trigger(id, QStringLiteral("B")));
    host.list.back().connected = false;
    CHECK_FALSE(commands.isEnabled(id, QStringLiteral("B")));
    CHECK_FALSE(commands.trigger(id, QStringLiteral("B")));
    CHECK(host.calls == QStringList{QStringLiteral("fullscreen B 1"), QStringLiteral("fullscreen B 0")});
}

TEST_CASE("the mini window belongs to the mirror window, has no default key and toggles for its device (INP-024)")
{
    FakeHost host;
    host.add(QStringLiteral("B")).connected = true;
    Commands commands(host);
    const QString id = QStringLiteral("device.mini");

    const Command* mini = commands.table().find(id);
    REQUIRE(mini != nullptr);
    CHECK(mini->scope == CommandScope::Mirror);
    CHECK(mini->target == gs::app::CommandTarget::Device);
    CHECK(mini->shortcut == QKeyCombination());
    CHECK(commands.title(id, QStringLiteral("B")) == QStringLiteral("Shrink to a mini window on top"));
    CHECK(commands.trigger(id, QStringLiteral("B")));
    host.list.back().mini = true;
    CHECK(commands.isChecked(id, QStringLiteral("B")));
    CHECK(commands.title(id, QStringLiteral("B")) == QStringLiteral("Restore from the mini window"));
    CHECK(commands.trigger(id, QStringLiteral("B")));
    CHECK(host.calls == QStringList{QStringLiteral("mini B 1"), QStringLiteral("mini B 0")});
}

TEST_CASE("key mapping and the camera lock belong to the mirror window, have no default keys and act on its device (ADR-0070)")
{
    FakeHost host;
    host.add(QStringLiteral("A"));
    host.add(QStringLiteral("B")).connected = true;
    Commands commands(host);
    const QString keymap = QStringLiteral("device.keymap");
    const QString camera = QStringLiteral("device.cameraLock");
    const QString hints = QStringLiteral("device.keyHints");

    for (const QString& id : {keymap, camera, hints}) {
        const Command* command = commands.table().find(id);
        REQUIRE(command != nullptr);
        CHECK(command->scope == CommandScope::Mirror);
        CHECK(command->target == gs::app::CommandTarget::Device);
        CHECK(command->shortcut == QKeyCombination());
    }
    CHECK(commands.match(QKeyCombination(Qt::Key_QuoteLeft), CommandScope::Mirror).isEmpty());

    CHECK_FALSE(commands.isEnabled(keymap, QStringLiteral("A")));
    CHECK_FALSE(commands.isEnabled(keymap, QStringLiteral("B")));
    CHECK_FALSE(commands.isEnabled(camera, QStringLiteral("B")));
    host.list.back().hasProfile = true;
    CHECK(commands.title(keymap, QStringLiteral("B")) == QStringLiteral("Turn on key mapping"));
    CHECK_FALSE(commands.isEnabled(camera, QStringLiteral("B")));
    CHECK(commands.trigger(keymap, QStringLiteral("B")));
    host.list.back().keymap = true;
    CHECK(commands.isChecked(keymap, QStringLiteral("B")));
    CHECK(commands.title(keymap, QStringLiteral("B")) == QStringLiteral("Turn off key mapping"));
    CHECK(commands.trigger(keymap, QStringLiteral("B")));

    CHECK(commands.isEnabled(camera, QStringLiteral("B")));
    CHECK(commands.trigger(camera, QStringLiteral("B")));

    CHECK(commands.isEnabled(hints, QStringLiteral("B")));
    CHECK(commands.title(hints, QStringLiteral("B")) == QStringLiteral("Show key hints"));
    CHECK(commands.trigger(hints, QStringLiteral("B")));
    host.list.back().keyHints = true;
    CHECK(commands.isChecked(hints, QStringLiteral("B")));
    CHECK(commands.title(hints, QStringLiteral("B")) == QStringLiteral("Hide key hints"));
    CHECK(commands.trigger(hints, QStringLiteral("B")));
    CHECK(host.calls == QStringList{QStringLiteral("keymap B 1"), QStringLiteral("keymap B 0"),
                                    QStringLiteral("camera B 1"), QStringLiteral("hints B 1"),
                                    QStringLiteral("hints B 0")});
}

TEST_CASE("the Edit/Try switch belongs to the editor, has no default key, and while trying only it and saving stay on (EDT-008)")
{
    FakeHost host;
    host.add(QStringLiteral("B")).connected = true;
    Commands commands(host);
    const QString id = QStringLiteral("editor.playtest");
    const Command* playtest = commands.table().find(id);
    REQUIRE(playtest != nullptr);
    CHECK(playtest->scope == CommandScope::Mirror);
    CHECK(playtest->target == gs::app::CommandTarget::Device);
    CHECK(playtest->shortcut == QKeyCombination());
    CHECK_FALSE(commands.isEnabled(id, QStringLiteral("B")));

    host.list.back().editing = true;
    CHECK(commands.title(id, QStringLiteral("B")) == QStringLiteral("Try the key mapping"));
    CHECK(commands.trigger(id, QStringLiteral("B")));
    host.list.back().trying = true;
    CHECK(commands.isChecked(id, QStringLiteral("B")));
    CHECK(commands.title(id, QStringLiteral("B")) == QStringLiteral("Back to editing the key mapping"));
    for (const char* inert : {"editor.undo", "editor.redo", "editor.delete", "editor.duplicate", "editor.selectAll"}) {
        CHECK_FALSE_MESSAGE(commands.isEnabled(QString::fromLatin1(inert), QStringLiteral("B")), inert);
    }
    CHECK(commands.isEnabled(QStringLiteral("editor.save"), QStringLiteral("B")));
    CHECK(commands.trigger(id, QStringLiteral("B")));
    CHECK(commands.trigger(QStringLiteral("editor.save"), QStringLiteral("B")));
    host.list.back().trying = false;
    CHECK(commands.isEnabled(QStringLiteral("editor.undo"), QStringLiteral("B")));
    host.list.back().logic = true;
    CHECK(commands.isEnabled(QStringLiteral("editor.undo"), QStringLiteral("B")));
    CHECK(commands.isEnabled(QStringLiteral("editor.redo"), QStringLiteral("B")));
    CHECK_FALSE(commands.isEnabled(QStringLiteral("editor.delete"), QStringLiteral("B")));
    host.list.back().logic = false;
    CHECK(host.calls == QStringList{QStringLiteral("editor B try"), QStringLiteral("editor B edit"),
                                    QStringLiteral("editor B save")});
}

TEST_CASE("the next and previous profile belong to the mirror window, have no default keys and need a profile (ADR-0070)")
{
    FakeHost host;
    host.add(QStringLiteral("A")).connected = true;
    Commands commands(host);
    const QString next = QStringLiteral("device.profile.next");
    const QString previous = QStringLiteral("device.profile.previous");
    for (const QString& id : {next, previous}) {
        const Command* command = commands.table().find(id);
        REQUIRE(command != nullptr);
        CHECK(command->scope == CommandScope::Mirror);
        CHECK(command->target == gs::app::CommandTarget::Device);
        CHECK(command->shortcut == QKeyCombination());
        CHECK_FALSE(commands.isEnabled(id, QStringLiteral("A")));
    }
    host.list.back().profiles = 2;
    CHECK(commands.trigger(next, QStringLiteral("A")));
    CHECK(commands.trigger(previous, QStringLiteral("A")));
    CHECK(host.calls == QStringList{QStringLiteral("profile A 1"), QStringLiteral("profile A -1")});
}

TEST_CASE("while typing into the phone only the command palette, key mapping and the camera lock keep their keys (ADR-0070)")
{
    FakeHost host;
    Commands commands(host);
    CHECK(gs::app::passesTyping(u"app.commandPalette"));
    CHECK(gs::app::passesTyping(u"device.keymap"));
    CHECK(gs::app::passesTyping(u"device.cameraLock"));
    CHECK(commands.table().find(QStringLiteral("app.commandPalette")) != nullptr);
    CHECK_FALSE(gs::app::passesTyping(u"device.fullscreen"));
    CHECK_FALSE(gs::app::passesTyping(u"device.back"));
    CHECK_FALSE(gs::app::passesTyping(u""));
}

TEST_CASE("F11 is free in the mirror scope, and full screen gives it up and gets it back like any default")
{
    FakeHost host;
    Commands commands(host);
    const QString id = QStringLiteral("device.fullscreen");
    const QKeyCombination f11(Qt::Key_F11);
    const auto conflicted = [&commands](const QString& command) {
        const std::vector<gs::app::ShortcutInfo> list = commands.shortcutList();
        const auto entry = std::ranges::find(list, command, &gs::app::ShortcutInfo::id);
        return entry != list.end() && entry->conflicted;
    };

    CHECK_FALSE(conflicted(id));
    CHECK(commands.conflicts(QStringLiteral("device.back"), QStringLiteral("F11")) ==
          QStringList{QStringLiteral("Full screen")});
    CHECK(commands.conflicts(QStringLiteral("settings.search"), QStringLiteral("F11")).isEmpty());
    CHECK(commands.conflicts(id, QStringLiteral("Ctrl+K")) == QStringList{QStringLiteral("Command palette")});

    CHECK(commands.setShortcut(QStringLiteral("settings.search"), QStringLiteral("F11")));
    CHECK(commands.match(f11, CommandScope::Hub) == QStringLiteral("settings.search"));
    CHECK(commands.match(f11, CommandScope::Mirror) == id);
    CHECK_FALSE(conflicted(id));

    CHECK(commands.setShortcut(QStringLiteral("device.back"), QStringLiteral("F11")));
    CHECK(commands.match(f11, CommandScope::Mirror) == QStringLiteral("device.back"));
    CHECK(commands.shortcutText(id).isEmpty());

    commands.resetShortcut(id);
    CHECK(commands.shortcutText(id).isEmpty());
    CHECK(conflicted(id));
    commands.resetAllShortcuts();
    CHECK_FALSE(commands.customized());
    CHECK(commands.shortcutText(id) == QStringLiteral("F11"));
    CHECK(commands.match(f11, CommandScope::Mirror) == id);
    CHECK(commands.match(f11, CommandScope::Hub).isEmpty());
}

TEST_CASE("volume steps stop at the ends and wait while muted")
{
    FakeHost host;
    DeviceFacts& phone = host.add(QStringLiteral("B"));
    phone.connected = true;
    phone.audio = true;
    phone.volume = 0.95;
    Commands commands(host);

    CHECK(commands.trigger(QStringLiteral("device.volumeUp"), QStringLiteral("B")));
    host.list.front().volume = 0.05;
    CHECK(commands.trigger(QStringLiteral("device.volumeDown"), QStringLiteral("B")));
    host.list.front().muted = true;
    CHECK_FALSE(commands.isEnabled(QStringLiteral("device.volumeUp"), QStringLiteral("B")));
    host.list.front().audio = false;
    host.list.front().muted = false;
    CHECK_FALSE(commands.isEnabled(QStringLiteral("device.mute"), QStringLiteral("B")));
    CHECK(host.calls == QStringList{QStringLiteral("volume B 1.0"), QStringLiteral("volume B 0.0")});
}

TEST_CASE("choice commands check the variant in effect")
{
    FakeHost host;
    DeviceFacts& phone = host.add(QStringLiteral("A"));
    phone.canConnect = true;
    phone.transport = 1;
    phone.rotationControl = false;
    Commands commands(host);

    CHECK(commands.isChecked(QStringLiteral("device.transport.wifi"), QStringLiteral("A")));
    CHECK_FALSE(commands.isChecked(QStringLiteral("device.transport.adb"), QStringLiteral("A")));
    CHECK(commands.trigger(QStringLiteral("device.transport.adb"), QStringLiteral("A")));
    CHECK_FALSE(commands.isEnabled(QStringLiteral("device.orientation.portrait"), QStringLiteral("A")));
    host.list.front().rotationControl = true;
    host.list.front().orientation = 2;
    CHECK(commands.isChecked(QStringLiteral("device.orientation.landscape"), QStringLiteral("A")));
    CHECK(commands.trigger(QStringLiteral("device.orientation.portrait"), QStringLiteral("A")));
    const Command* portrait = commands.table().find(u"device.orientation.portrait");
    REQUIRE(portrait != nullptr);
    CHECK(portrait->group == QStringLiteral("device.orientation"));
    CHECK(host.calls == QStringList{QStringLiteral("transport A 0"), QStringLiteral("orientation A 1")});
}

TEST_CASE("ADR-0107: the connection method commands are off for capture cards and ESP32 boards")
{
    FakeHost host;
    host.add(QStringLiteral("A")).canConnect = true;
    DeviceFacts& capture = host.add(QStringLiteral("capture-1a2b3c4d"));
    capture.canConnect = true;
    capture.transportChoice = false;
    capture.deviceId = QStringLiteral("capture-1a2b3c4d");
    DeviceFacts& board = host.add(QStringLiteral("esp32-COM5"));
    board.canConnect = true;
    board.transportChoice = false;
    Commands commands(host);

    for (const QString& id : {QStringLiteral("device.transport.adb"), QStringLiteral("device.transport.wifi"),
                              QStringLiteral("device.transport.auto")}) {
        CAPTURE(id.toStdString());
        CHECK(commands.isEnabled(id, QStringLiteral("A")));
        CHECK_FALSE(commands.isEnabled(id, QStringLiteral("capture-1a2b3c4d")));
        CHECK_FALSE(commands.trigger(id, QStringLiteral("capture-1a2b3c4d")));
        CHECK_FALSE(commands.isEnabled(id, QStringLiteral("esp32-COM5")));
        CHECK_FALSE(commands.trigger(id, QStringLiteral("esp32-COM5")));
    }
    CHECK(commands.isEnabled(QStringLiteral("device.mirror"), QStringLiteral("capture-1a2b3c4d")));
    CHECK(commands.isEnabled(QStringLiteral("device.mirror"), QStringLiteral("esp32-COM5")));
    CHECK(commands.isEnabled(QStringLiteral("device.settings"), QStringLiteral("capture-1a2b3c4d")));
    CHECK(host.calls.isEmpty());
    CHECK(DeviceFacts{}.transportChoice);
}

TEST_CASE("commands the interface carries out are requested with their target")
{
    FakeHost host;
    host.add(QStringLiteral("A")).deviceId = QStringLiteral("aid:1");
    host.add(QStringLiteral("10.0.0.2:5555")).wireless = true;
    Commands commands(host);
    QStringList requests;
    QObject::connect(&commands, &Commands::uiRequested, &commands,
                     [&requests](const QString& id, const QString& target) { requests.append(id + u' ' + target); });

    CHECK(commands.trigger(QStringLiteral("device.settings"), QStringLiteral("A")));
    CHECK_FALSE(commands.isEnabled(QStringLiteral("device.settings"), QStringLiteral("10.0.0.2:5555")));
    CHECK(commands.trigger(QStringLiteral("hub.settings")));
    CHECK(commands.trigger(QStringLiteral("settings.search")));
    CHECK(commands.trigger(QStringLiteral("device.endWireless"), QStringLiteral("10.0.0.2:5555")));
    CHECK_FALSE(commands.isEnabled(QStringLiteral("adb.retry")));
    host.retry = true;
    CHECK(commands.trigger(QStringLiteral("adb.retry")));
    CHECK(requests == QStringList{QStringLiteral("device.settings A"), QStringLiteral("hub.settings "),
                                  QStringLiteral("settings.search ")});
    CHECK(host.calls == QStringList{QStringLiteral("end 10.0.0.2:5555"), QStringLiteral("retry")});
}

TEST_CASE("without a picture the key editor still opens and recording does not; the extensions page can be asked for "
          "one extension's panel (ADR-0105)")
{
    FakeHost host;
    DeviceFacts& board = host.add(QStringLiteral("esp32-COM5"));
    board.connected = true;
    board.hasProfile = true;
    board.noPicture = true;
    Commands commands(host);
    QStringList requests;
    QObject::connect(&commands, &Commands::uiRequested, &commands,
                     [&requests](const QString& id, const QString& target) { requests.append(id + u' ' + target); });

    CHECK(commands.isEnabled(QStringLiteral("device.editKeymap"), QStringLiteral("esp32-COM5")));
    CHECK_FALSE(commands.isEnabled(QStringLiteral("mirror.record"), QStringLiteral("esp32-COM5")));
    CHECK(commands.trigger(QStringLiteral("device.editKeymap"), QStringLiteral("esp32-COM5")));
    CHECK(host.calls == QStringList{QStringLiteral("editing esp32-COM5 1")});

    CHECK(commands.trigger(QStringLiteral("hub.extensions"), QStringLiteral("gamescrcpy.esp32")));
    CHECK(commands.trigger(QStringLiteral("hub.extensions")));
    CHECK(requests ==
          QStringList{QStringLiteral("hub.extensions gamescrcpy.esp32"), QStringLiteral("hub.extensions ")});
}

TEST_CASE("ADR-0107: the ESP32 settings command is offered only while the ESP32 extension is on and asks the interface "
          "for the settings sheet")
{
    FakeHost host;
    host.add(QStringLiteral("esp32-COM5")).connected = true;
    Commands commands(host);
    QStringList requests;
    QObject::connect(&commands, &Commands::uiRequested, &commands,
                     [&requests](const QString& id, const QString& target) { requests.append(id + u' ' + target); });
    const QString id = QStringLiteral("esp32.settings");

    const Command* settings = commands.table().find(id);
    REQUIRE(settings != nullptr);
    CHECK(settings->scope == CommandScope::Global);
    CHECK(settings->target == gs::app::CommandTarget::None);
    CHECK(settings->shortcut == QKeyCombination());
    CHECK(commands.title(id) == QStringLiteral("ESP32 touch box settings"));
    CHECK(commands.icon(id) == QStringLiteral("usb"));
    CHECK_FALSE(commands.isEnabled(id));
    CHECK_FALSE(commands.trigger(id));

    {
        gs::app::Esp32Boards boards(gs::app::Esp32Hooks{.watchUsb = false});
        CHECK_FALSE(commands.isEnabled(id));
        boards.setActive(true);
        CHECK(commands.isEnabled(id));
        CHECK(commands.trigger(id, QStringLiteral("esp32-COM5")));
        boards.setActive(false);
        CHECK_FALSE(commands.isEnabled(id));
        CHECK_FALSE(commands.trigger(id));
        boards.setActive(true);
        CHECK(commands.isEnabled(id));
    }
    CHECK_FALSE(commands.isEnabled(id));
    CHECK_FALSE(commands.trigger(id));
    CHECK(requests == QStringList{QStringLiteral("esp32.settings ")});
    CHECK(host.calls.isEmpty());
}

TEST_CASE("the grid needs two phones mirroring and tiling one; the interface carries both out (ADR-0096)")
{
    FakeHost host;
    host.add(QStringLiteral("A")).connected = true;
    host.add(QStringLiteral("B"));
    Commands commands(host);
    QStringList requests;
    QObject::connect(&commands, &Commands::uiRequested, &commands,
                     [&requests](const QString& id, const QString& target) { requests.append(id + u' ' + target); });

    CHECK_FALSE(commands.isEnabled(QStringLiteral("devices.grid")));
    CHECK_FALSE(commands.trigger(QStringLiteral("devices.grid")));
    CHECK(commands.isEnabled(QStringLiteral("devices.tile")));
    host.list[1].connected = true;
    CHECK(commands.isEnabled(QStringLiteral("devices.grid")));
    CHECK(commands.trigger(QStringLiteral("devices.grid")));
    CHECK(commands.trigger(QStringLiteral("devices.tile"), QStringLiteral("A")));
    CHECK(requests == QStringList{QStringLiteral("devices.grid "), QStringLiteral("devices.tile ")});
    CHECK(host.calls.isEmpty());
    CHECK(commands.title(QStringLiteral("devices.grid")) == QStringLiteral("View all phones in a grid"));
    CHECK(commands.title(QStringLiteral("devices.tile")) == QStringLiteral("Tile all mirror windows"));
    CHECK(commands.shortcutText(QStringLiteral("devices.grid")).isEmpty());
    CHECK(commands.shortcutText(QStringLiteral("devices.tile")).isEmpty());
    const Command* grid = commands.table().find(u"devices.grid");
    REQUIRE(grid != nullptr);
    CHECK(grid->scope == CommandScope::Global);
    CHECK(grid->target == gs::app::CommandTarget::None);
    host.list.clear();
    CHECK_FALSE(commands.isEnabled(QStringLiteral("devices.tile")));
    CHECK_FALSE(commands.isEnabled(QStringLiteral("devices.grid")));
}

TEST_CASE("the apps page opens for a phone whose server lists apps")
{
    FakeHost host;
    host.add(QStringLiteral("A")).connected = true;
    DeviceFacts& listing = host.add(QStringLiteral("B"));
    listing.connected = true;
    listing.apps = true;
    Commands commands(host);
    QStringList requests;
    QObject::connect(&commands, &Commands::uiRequested, &commands,
                     [&requests](const QString& id, const QString& target) { requests.append(id + u' ' + target); });

    CHECK_FALSE(commands.isEnabled(QStringLiteral("device.apps"), QStringLiteral("A")));
    CHECK_FALSE(commands.trigger(QStringLiteral("device.apps"), QStringLiteral("A")));
    CHECK(commands.isEnabled(QStringLiteral("device.apps"), QStringLiteral("B")));
    CHECK(commands.trigger(QStringLiteral("device.apps"), QStringLiteral("B")));
    CHECK(commands.title(QStringLiteral("device.apps"), QStringLiteral("B")) == QStringLiteral("Apps"));
    CHECK(requests == QStringList{QStringLiteral("device.apps B")});
}

TEST_CASE("the files page opens for a phone online in adb, mirrored or not")
{
    FakeHost host;
    host.add(QStringLiteral("A")).connected = true;
    host.add(QStringLiteral("B")).files = true;
    Commands commands(host);
    QStringList requests;
    QObject::connect(&commands, &Commands::uiRequested, &commands,
                     [&requests](const QString& id, const QString& target) { requests.append(id + u' ' + target); });

    CHECK_FALSE(commands.isEnabled(QStringLiteral("device.files"), QStringLiteral("A")));
    CHECK_FALSE(commands.trigger(QStringLiteral("device.files"), QStringLiteral("A")));
    CHECK(commands.isEnabled(QStringLiteral("device.files"), QStringLiteral("B")));
    CHECK(commands.trigger(QStringLiteral("device.files"), QStringLiteral("B")));
    CHECK(commands.title(QStringLiteral("device.files"), QStringLiteral("B")) == QStringLiteral("Files"));
    CHECK(commands.icon(QStringLiteral("device.files")) == QStringLiteral("folder-open"));
    CHECK(requests == QStringList{QStringLiteral("device.files B")});
}

TEST_CASE("the current device is the last one acted on, else the only one mirroring, else the only one listed")
{
    FakeHost host;
    Commands commands(host);
    CHECK(commands.currentDevice().isEmpty());

    host.add(QStringLiteral("A"));
    CHECK(commands.currentDevice() == QStringLiteral("A"));
    host.add(QStringLiteral("B"));
    CHECK(commands.currentDevice().isEmpty());
    CHECK_FALSE(commands.trigger(QStringLiteral("device.mirror")));

    host.list[1].connected = true;
    CHECK(commands.currentDevice() == QStringLiteral("B"));
    CHECK(commands.trigger(QStringLiteral("device.back")));

    host.list[0].canConnect = true;
    int changes = 0;
    QObject::connect(&commands, &Commands::stateChanged, &commands, [&changes] { ++changes; });
    CHECK(commands.trigger(QStringLiteral("device.mirror"), QStringLiteral("A")));
    CHECK(changes == 1);
    CHECK(commands.currentDevice() == QStringLiteral("A"));

    host.list.erase(host.list.begin());
    CHECK(commands.currentDevice() == QStringLiteral("B"));
    CHECK(host.calls == QStringList{QStringLiteral("key B 4"), QStringLiteral("connect A")});
}

TEST_CASE("shortcuts read as their keys")
{
    FakeHost host;
    const Commands commands(host);
    CHECK(commands.shortcutText(QStringLiteral("settings.search")) == QStringLiteral("Ctrl F"));
    CHECK(commands.shortcutText(QStringLiteral("device.mute")).isEmpty());
    CHECK(Commands::keyText(QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_S)) ==
          QStringLiteral("Ctrl Shift S"));
    CHECK(Commands::keyText(QKeyCombination(Qt::AltModifier, Qt::Key_F4)) == QStringLiteral("Alt F4"));
    CHECK(Commands::keyText(QKeyCombination()).isEmpty());
}

TEST_CASE("a changed shortcut replaces the default, an emptied one removes it, and unreadable ones are kept unused")
{
    FakeHost host;
    Commands commands(host);
    ShortcutMap stored{{"untouched", "yes"}};
    commands.setShortcutStore([&stored](const ShortcutMap& shortcuts) { stored = shortcuts; });

    commands.applyUserShortcuts({{"device.mute", "Ctrl+M"},
                                 {"settings.search", ""},
                                 {"device.missing", "F1"},
                                 {"device.home", "Ctrl+X, Ctrl+C"},
                                 {"device.back", "nonsense"}});
    CHECK(commands.shortcutText(QStringLiteral("device.mute")) == QStringLiteral("Ctrl M"));
    CHECK(commands.shortcutText(QStringLiteral("settings.search")).isEmpty());
    CHECK(commands.shortcutText(QStringLiteral("device.home")).isEmpty());
    CHECK(stored == ShortcutMap{{"untouched", "yes"}});

    CHECK(commands.setShortcut(QStringLiteral("device.back"), QStringLiteral("Alt+B")));
    CHECK(stored == ShortcutMap{{"device.back", "Alt+B"},
                                {"device.home", "Ctrl+X, Ctrl+C"},
                                {"device.missing", "F1"},
                                {"device.mute", "Ctrl+M"},
                                {"settings.search", ""}});
    CHECK(commands.shortcutText(QStringLiteral("device.back")) == QStringLiteral("Alt B"));
    CHECK_FALSE(commands.setShortcut(QStringLiteral("device.back"), QStringLiteral("Ctrl+X, Ctrl+C")));
    CHECK_FALSE(commands.setShortcut(QStringLiteral("device.back"), QStringLiteral("Shift")));
    CHECK_FALSE(commands.setShortcut(QStringLiteral("device.nothing"), QStringLiteral("F2")));

    CHECK(commands.setShortcut(QStringLiteral("settings.search"), QStringLiteral("Ctrl+F")));
    CHECK_FALSE(stored.contains("settings.search"));
    CHECK(commands.setShortcut(QStringLiteral("device.back"), QString()));
    CHECK_FALSE(stored.contains("device.back"));
    commands.resetShortcut(QStringLiteral("device.mute"));
    CHECK(commands.shortcutText(QStringLiteral("device.mute")).isEmpty());
    CHECK(commands.setShortcut(QStringLiteral("settings.search"), QString()));
    CHECK(stored ==
          ShortcutMap{{"device.home", "Ctrl+X, Ctrl+C"}, {"device.missing", "F1"}, {"settings.search", ""}});
    commands.resetAllShortcuts();
    CHECK(stored == ShortcutMap{{"device.missing", "F1"}});
    CHECK(commands.shortcutText(QStringLiteral("settings.search")) == QStringLiteral("Ctrl F"));
}

TEST_CASE("shortcuts read from the file that repeat the default, or empty a command without one, change nothing")
{
    FakeHost host;
    Commands commands(host);
    ShortcutMap stored;
    commands.setShortcutStore([&stored](const ShortcutMap& shortcuts) { stored = shortcuts; });
    commands.applyUserShortcuts({{"settings.search", "Ctrl+F"}, {"device.mute", ""}, {"device.home", "Ctrl+H"}});
    const auto custom = [&commands](const char* id) {
        const std::vector<gs::app::ShortcutInfo> list = commands.shortcutList();
        const auto found = std::ranges::find(list, QString::fromLatin1(id), &gs::app::ShortcutInfo::id);
        return found != list.end() && found->custom;
    };
    CHECK_FALSE(custom("settings.search"));
    CHECK_FALSE(custom("device.mute"));
    CHECK(custom("device.home"));
    CHECK(commands.customized());
    CHECK(commands.setShortcut(QStringLiteral("device.back"), QStringLiteral("Alt+B")));
    CHECK(stored == ShortcutMap{{"device.back", "Alt+B"}, {"device.home", "Ctrl+H"}});
    commands.resetAllShortcuts();
    CHECK_FALSE(commands.customized());
}

TEST_CASE("keys show as one cap each, names of several words included, and keys without Ctrl, Alt or Win are plain")
{
    CHECK(Commands::keyCaps(QKeyCombination(Qt::ControlModifier, Qt::Key_VolumeUp)) ==
          QStringList{QStringLiteral("Ctrl"), QStringLiteral("Volume Up")});
    CHECK(Commands::keyCaps(QKeyCombination(Qt::AltModifier | Qt::ShiftModifier, Qt::Key_Left)) ==
          QStringList{QStringLiteral("Alt"), QStringLiteral("Shift"), QStringLiteral(u"←")});
    CHECK(Commands::keyCaps(QKeyCombination(Qt::Key_Space)) == QStringList{QStringLiteral("Space")});
    CHECK(Commands::keyCaps(QKeyCombination()).isEmpty());
    CHECK(Commands::keyText(QKeyCombination(Qt::MetaModifier, Qt::Key_PageUp)) == QStringLiteral("Win PgUp"));
    CHECK(Commands::plain(QKeyCombination(Qt::Key_Space)));
    CHECK(Commands::plain(QKeyCombination(Qt::ShiftModifier, Qt::Key_D)));
    CHECK_FALSE(Commands::plain(QKeyCombination(Qt::ControlModifier, Qt::Key_D)));
    CHECK_FALSE(Commands::plain(QKeyCombination(Qt::MetaModifier, Qt::Key_Return)));
    CHECK_FALSE(Commands::plain(QKeyCombination(Qt::Key_F5)));
    CHECK_FALSE(Commands::plain(QKeyCombination(Qt::Key_VolumeUp)));
    CHECK_FALSE(Commands::plain(QKeyCombination()));
}

TEST_CASE("the shortcut list updates a changed row in place and narrows to what the settings search found")
{
    FakeHost host;
    Commands commands(host);
    gs::app::ShortcutModel device;
    device.setDevice(true);
    gs::app::ShortcutModel hub;
    int resets = 0;
    int changes = 0;
    QObject::connect(&hub, &QAbstractItemModel::modelReset, &hub, [&resets] { ++resets; });
    QObject::connect(&hub, &QAbstractItemModel::dataChanged, &hub, [&changes] { ++changes; });
    const auto rowOf = [](const gs::app::ShortcutModel& model, const char* id) {
        for (int row = 0; row < model.rowCount(); ++row) {
            if (model.data(model.index(row), gs::app::ShortcutModel::CommandIdRole).toString() == QLatin1StringView(id)) {
                return row;
            }
        }
        return -1;
    };
    CHECK(rowOf(device, "device.mute") >= 0);
    CHECK(rowOf(device, "settings.search") < 0);
    const int search = rowOf(hub, "settings.search");
    REQUIRE(search >= 0);
    const QModelIndex at = hub.index(search);
    CHECK(hub.data(at, gs::app::ShortcutModel::KeysRole).toStringList() ==
          QStringList{QStringLiteral("Ctrl"), QStringLiteral("F")});
    CHECK(hub.data(at, gs::app::ShortcutModel::DefaultPortableRole).toString() == QStringLiteral("Ctrl+F"));
    CHECK(hub.data(at, gs::app::ShortcutModel::DefaultTextRole).toString() == QStringLiteral("Ctrl F"));

    CHECK(commands.setShortcut(QStringLiteral("settings.search"), QStringLiteral("F3")));
    CHECK(resets == 0);
    CHECK(changes == 1);
    CHECK(hub.data(at, gs::app::ShortcutModel::KeysRole).toStringList() == QStringList{QStringLiteral("F3")});
    CHECK(hub.data(at, gs::app::ShortcutModel::CustomRole).toBool());

    hub.setFiltered(true);
    hub.setHits({{QStringLiteral("shortcuts.settings.search"), true}});
    CHECK(hub.count() == 1);
    hub.setHits({{QStringLiteral("shortcuts"), true}});
    CHECK(hub.count() > 1);
    hub.setFiltered(false);
    CHECK(rowOf(hub, "settings.search") == search);
}

TEST_CASE("the settings search finds shortcuts by their command and by the section's own words")
{
    FakeHost host;
    const Commands commands(host);
    gs::app::SettingsCatalog catalog;
    catalog.setQuery(QStringLiteral("mute"));
    CHECK(catalog.hits().contains(QStringLiteral("shortcuts.device.mute")));
    CHECK(catalog.pageHits().value(QStringLiteral("shortcuts")).toInt() >= 1);
    catalog.setQuery(QStringLiteral("hotkey"));
    CHECK(catalog.hits().contains(QStringLiteral("shortcuts")));
    catalog.setQuery(QStringLiteral("theme"));
    CHECK(catalog.pageHits().value(QStringLiteral("shortcuts")).toInt() == 0);
}

TEST_CASE("a key in use goes to the user's choice over a default, else to the earlier command, where scopes overlap")
{
    FakeHost host;
    Commands commands(host);
    const QKeyCombination ctrlF(Qt::ControlModifier, Qt::Key_F);
    const QKeyCombination ctrlM(Qt::ControlModifier, Qt::Key_M);
    const auto conflicted = [&commands](const QString& id) {
        const std::vector<gs::app::ShortcutInfo> list = commands.shortcutList();
        const auto entry = std::ranges::find(list, id, &gs::app::ShortcutInfo::id);
        return entry != list.end() && entry->conflicted;
    };

    CHECK(commands.match(ctrlF, CommandScope::Hub) == QStringLiteral("settings.search"));
    CHECK(commands.match(ctrlF, CommandScope::Mirror).isEmpty());
    CHECK(commands.match(QKeyCombination(Qt::Key_F), CommandScope::Hub).isEmpty());

    commands.applyUserShortcuts({{"device.mute", "Ctrl+F"}});
    CHECK(commands.match(ctrlF, CommandScope::Hub) == QStringLiteral("device.mute"));
    CHECK(commands.match(ctrlF, CommandScope::Mirror) == QStringLiteral("device.mute"));
    CHECK(commands.shortcutText(QStringLiteral("settings.search")).isEmpty());
    CHECK(conflicted(QStringLiteral("settings.search")));
    CHECK_FALSE(conflicted(QStringLiteral("device.mute")));

    commands.applyUserShortcuts({{"device.mute", "Ctrl+M"}, {"device.home", "Ctrl+M"}});
    CHECK(commands.match(ctrlM, CommandScope::Mirror) == QStringLiteral("device.mute"));
    CHECK(conflicted(QStringLiteral("device.home")));
    CHECK(commands.match(ctrlF, CommandScope::Hub) == QStringLiteral("settings.search"));
    CHECK(commands.conflicts(QStringLiteral("device.back"), QStringLiteral("Ctrl+M")) ==
          QStringList{QStringLiteral("Mute"), QStringLiteral("Home")});
    CHECK(commands.conflicts(QStringLiteral("device.back"), QStringLiteral("Ctrl+B")).isEmpty());

    CHECK(commands.setShortcut(QStringLiteral("device.back"), QStringLiteral("Ctrl+M")));
    CHECK(commands.match(ctrlM, CommandScope::Mirror) == QStringLiteral("device.back"));
    CHECK(commands.shortcutText(QStringLiteral("device.mute")).isEmpty());
    CHECK_FALSE(conflicted(QStringLiteral("device.home")));
    CHECK(commands.shortcutText(QStringLiteral("device.home")).isEmpty());
}

TEST_CASE("pressed keys read as the key that carries the label, without the keypad flag or lone modifiers")
{
    const QKeyEvent shifted(QEvent::KeyPress, Qt::Key_Exclam, Qt::ControlModifier | Qt::ShiftModifier, 0x02, '1', 0);
    CHECK(Commands::keysOf(shifted) == QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_1));
    const QKeyEvent letter(QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier, 0x25, 'K', 0);
    CHECK(Commands::keysOf(letter) == QKeyCombination(Qt::ControlModifier, Qt::Key_K));
    const QKeyEvent keypad(QEvent::KeyPress, Qt::Key_5, Qt::KeypadModifier, 0x4c, 0x65, 0);
    CHECK(Commands::keysOf(keypad) == QKeyCombination(Qt::Key_5));
    const QKeyEvent shift(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier, 0x2a, 0x10, 0);
    CHECK(Commands::keysOf(shift) == QKeyCombination());
    CHECK(Commands::parseKeys(QStringLiteral("Ctrl+Shift+S")) ==
          QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_S));
    CHECK(Commands::portableKeys(QKeyCombination(Qt::AltModifier, Qt::Key_F4)) == QStringLiteral("Alt+F4"));
    CHECK(Commands::portableKeys(QKeyCombination()).isEmpty());
}

TEST_CASE("the notification shade commands need a phone with the server, have no default keys and ask for their "
          "panel (ADR-0105)")
{
    FakeHost host;
    host.add(QStringLiteral("A")).connected = true;
    DeviceFacts& phone = host.add(QStringLiteral("B"));
    phone.connected = true;
    phone.systemPanel = true;
    Commands commands(host);
    const QString notifications = QStringLiteral("device.notifications");
    const QString quickSettings = QStringLiteral("device.quickSettings");
    const QString collapse = QStringLiteral("device.collapsePanels");

    for (const QString& id : {notifications, quickSettings, collapse}) {
        const Command* command = commands.table().find(id);
        REQUIRE(command != nullptr);
        CHECK(command->target == gs::app::CommandTarget::Device);
        CHECK(command->shortcut == QKeyCombination());
        CHECK(commands.shortcutText(id).isEmpty());
        CHECK_FALSE(commands.isEnabled(id, QStringLiteral("A")));
        CHECK_FALSE(commands.trigger(id, QStringLiteral("A")));
        CHECK(commands.isEnabled(id, QStringLiteral("B")));
    }
    CHECK(commands.title(notifications, QStringLiteral("B")) == QStringLiteral("Pull down the notification shade"));
    CHECK(commands.title(quickSettings, QStringLiteral("B")) == QStringLiteral("Quick settings"));
    CHECK(commands.title(collapse, QStringLiteral("B")) == QStringLiteral("Close the notification shade"));
    CHECK(commands.icon(notifications) == QStringLiteral("shade-open"));
    CHECK(commands.icon(quickSettings) == QStringLiteral("shade-tiles"));
    CHECK(commands.icon(collapse) == QStringLiteral("shade-close"));

    CHECK(commands.trigger(notifications, QStringLiteral("B")));
    CHECK(commands.trigger(quickSettings, QStringLiteral("B")));
    CHECK(commands.trigger(collapse, QStringLiteral("B")));
    CHECK(host.calls ==
          QStringList{QStringLiteral("panel B 0"), QStringLiteral("panel B 1"), QStringLiteral("panel B 2")});

    host.list.back().systemPanel = false;
    CHECK_FALSE(commands.isEnabled(notifications, QStringLiteral("B")));
    CHECK_FALSE(commands.trigger(collapse, QStringLiteral("B")));
    CHECK(host.calls.size() == 3);
}
