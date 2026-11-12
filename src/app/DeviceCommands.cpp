#include "DeviceCommands.h"

#include "TranslateNoop.h"

#include <algorithm>
#include <utility>

namespace gs::app {

namespace {

constexpr int kKeyHome = 3;
constexpr int kKeyBack = 4;
constexpr int kKeyAppSwitch = 187;
constexpr double kVolumeStep = 0.1;

using Predicate = std::function<bool(const DeviceFacts&)>;
using Action = std::function<void(const DeviceFacts&)>;

struct DeviceCommand {
    const char* id;
    const char* title;
    const char* checkedTitle = nullptr;
    const char* keywords = "";
    const char* icon = "";
    const char* group = "";
    const char* groupTitle = nullptr;
    CommandScope scope = CommandScope::Global;
    QKeyCombination shortcut;
    Predicate enabled;
    Predicate checked;
    Action run;
};

void add(CommandTable& table, DeviceCommandHost& host, const DeviceCommand& spec)
{
    Command command;
    command.id = QString::fromLatin1(spec.id);
    command.title = spec.title;
    command.checkedTitle = spec.checkedTitle;
    command.keywords = spec.keywords;
    command.icon = QString::fromLatin1(spec.icon);
    command.group = QString::fromLatin1(spec.group);
    command.groupTitle = spec.groupTitle;
    command.scope = spec.scope;
    command.target = CommandTarget::Device;
    command.shortcut = spec.shortcut;
    command.enabled = [&host, enabled = spec.enabled](const QString& serial) {
        const auto facts = host.device(serial);
        return facts && enabled(*facts);
    };
    if (spec.checked) {
        command.checked = [&host, checked = spec.checked](const QString& serial) {
            const auto facts = host.device(serial);
            return facts && checked(*facts);
        };
    }
    command.run = [&host, run = spec.run](const QString& serial) {
        if (const auto facts = host.device(serial)) {
            run(*facts);
        }
    };
    [[maybe_unused]] const bool added = table.add(std::move(command));
    Q_ASSERT_X(added, "addDeviceCommands", "invalid or duplicate command");
}

void addUi(CommandTable& table, const UiRequest& request, const char* id, const char* title, const char* keywords,
           const char* icon, CommandScope scope = CommandScope::Global, QKeyCombination shortcut = {},
           CommandTarget target = CommandTarget::None)
{
    Command command;
    command.id = QString::fromLatin1(id);
    command.title = title;
    command.keywords = keywords;
    command.icon = QString::fromLatin1(icon);
    command.scope = scope;
    command.target = target;
    command.shortcut = shortcut;
    command.enabled = [](const QString&) { return true; };
    command.run = [request, id = command.id](const QString& target) { request(id, target); };
    [[maybe_unused]] const bool added = table.add(std::move(command));
    Q_ASSERT_X(added, "addHubCommands", "invalid or duplicate command");
}

bool canControlAudio(const DeviceFacts& facts)
{
    return facts.connected && facts.audio;
}

}

void addDeviceCommands(CommandTable& table, DeviceCommandHost& host, const UiRequest& request)
{
    const auto connected = [](const DeviceFacts& facts) { return facts.connected; };
    const auto canConnect = [](const DeviceFacts& facts) { return facts.canConnect; };
    const auto rotation = [](const DeviceFacts& facts) { return facts.rotationControl; };

    add(table, host,
        {.id = "device.mirror",
         .title = GS_TRANSLATE_NOOP("Commands", "Mirror"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "connect start screen"),
         .icon = "monitor-play",
         .enabled = canConnect,
         .run = [&host](const DeviceFacts& facts) { host.connectDevice(facts.serial); }});
    add(table, host,
        {.id = "device.cancel",
         .title = GS_TRANSLATE_NOOP("Commands", "Cancel connecting"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "stop abort"),
         .icon = "x",
         .enabled = [](const DeviceFacts& facts) { return facts.busy; },
         .run = [&host](const DeviceFacts& facts) { host.disconnectDevice(facts.serial); }});
    add(table, host,
        {.id = "device.disconnect",
         .title = GS_TRANSLATE_NOOP("Commands", "Disconnect"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "stop end close"),
         .icon = "x",
         .enabled = connected,
         .run = [&host](const DeviceFacts& facts) { host.disconnectDevice(facts.serial); }});
    add(table, host,
        {.id = "device.showWindow",
         .title = GS_TRANSLATE_NOOP("Commands", "Show window"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "raise focus mirror"),
         .icon = "monitor",
         .enabled = connected,
         .run = [&host](const DeviceFacts& facts) { host.showWindow(facts.serial); }});

    struct Variant {
        const char* id;
        const char* title;
        const char* icon;
    };
    const Variant transports[] = {
        {"device.transport.adb", GS_TRANSLATE_NOOP("Commands", "Connect through the adb tunnel"), "plug"},
        {"device.transport.wifi", GS_TRANSLATE_NOOP("Commands", "Connect over WiFi direct"), "wifi"},
        {"device.transport.auto", GS_TRANSLATE_NOOP("Commands", "Choose the connection automatically"), "link"},
    };
    for (int choice = 0; choice < static_cast<int>(std::size(transports)); ++choice) {
        add(table, host,
            {.id = transports[choice].id,
             .title = transports[choice].title,
             .keywords = GS_TRANSLATE_NOOP("Commands", "connection method transport usb wireless"),
             .icon = transports[choice].icon,
             .group = "device.transport",
             .groupTitle = GS_TRANSLATE_NOOP("Commands", "Connection method"),
             .enabled = [](const DeviceFacts& facts) { return facts.canConnect && facts.transportChoice; },
             .checked = [choice](const DeviceFacts& facts) { return facts.transport == choice; },
             .run = [&host, choice](const DeviceFacts& facts) { host.setTransport(facts.serial, choice); }});
    }

    add(table, host,
        {.id = "device.mute",
         .title = GS_TRANSLATE_NOOP("Commands", "Mute"),
         .checkedTitle = GS_TRANSLATE_NOOP("Commands", "Unmute"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "sound audio silence"),
         .icon = "volume-2",
         .enabled = canControlAudio,
         .checked = [](const DeviceFacts& facts) { return facts.muted; },
         .run = [&host](const DeviceFacts& facts) { host.setMuted(facts.serial, !facts.muted); }});
    add(table, host,
        {.id = "device.volumeUp",
         .title = GS_TRANSLATE_NOOP("Commands", "Volume up"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "sound louder"),
         .icon = "plus",
         .enabled = [](const DeviceFacts& facts) { return canControlAudio(facts) && !facts.muted; },
         .run = [&host](const DeviceFacts& facts) {
             host.setVolume(facts.serial, std::min(1.0, facts.volume + kVolumeStep));
         }});
    add(table, host,
        {.id = "device.volumeDown",
         .title = GS_TRANSLATE_NOOP("Commands", "Volume down"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "sound quieter"),
         .icon = "minus",
         .enabled = [](const DeviceFacts& facts) { return canControlAudio(facts) && !facts.muted; },
         .run = [&host](const DeviceFacts& facts) {
             host.setVolume(facts.serial, std::max(0.0, facts.volume - kVolumeStep));
         }});
    add(table, host,
        {.id = "device.screenOff",
         .title = GS_TRANSLATE_NOOP("Commands", "Turn the screen off"),
         .checkedTitle = GS_TRANSLATE_NOOP("Commands", "Turn the screen on"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "display power dark"),
         .icon = "power",
         .enabled = [](const DeviceFacts& facts) { return facts.screenControl; },
         .checked = [](const DeviceFacts& facts) { return facts.screenOff; },
         .run = [&host](const DeviceFacts& facts) { host.setScreenOff(facts.serial, !facts.screenOff); }});

    const Variant orientations[] = {
        {"device.orientation.follow", GS_TRANSLATE_NOOP("Commands", "Follow the device's rotation"), "rotate-cw"},
        {"device.orientation.portrait", GS_TRANSLATE_NOOP("Commands", "Keep portrait"), "rotate-cw"},
        {"device.orientation.landscape", GS_TRANSLATE_NOOP("Commands", "Keep landscape"), "rotate-cw"},
    };
    for (int choice = 0; choice < static_cast<int>(std::size(orientations)); ++choice) {
        add(table, host,
            {.id = orientations[choice].id,
             .title = orientations[choice].title,
             .keywords = GS_TRANSLATE_NOOP("Commands", "rotation orientation lock"),
             .icon = orientations[choice].icon,
             .group = "device.orientation",
             .groupTitle = GS_TRANSLATE_NOOP("Commands", "Orientation"),
             .enabled = rotation,
             .checked = [choice](const DeviceFacts& facts) { return facts.orientation == choice; },
             .run = [&host, choice](const DeviceFacts& facts) { host.setOrientation(facts.serial, choice); }});
    }

    add(table, host,
        {.id = "device.back",
         .title = GS_TRANSLATE_NOOP("Commands", "Back"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "android key return"),
         .icon = "arrow-left",
         .enabled = connected,
         .run = [&host](const DeviceFacts& facts) { host.pressKey(facts.serial, kKeyBack); }});
    add(table, host,
        {.id = "device.home",
         .title = GS_TRANSLATE_NOOP("Commands", "Home"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "android key launcher"),
         .icon = "house",
         .enabled = connected,
         .run = [&host](const DeviceFacts& facts) { host.pressKey(facts.serial, kKeyHome); }});
    add(table, host,
        {.id = "device.appSwitch",
         .title = GS_TRANSLATE_NOOP("Commands", "Recent apps"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "android key overview multitasking"),
         .icon = "square",
         .enabled = connected,
         .run = [&host](const DeviceFacts& facts) { host.pressKey(facts.serial, kKeyAppSwitch); }});
    const auto panels = [](const DeviceFacts& facts) { return facts.systemPanel; };
    add(table, host,
        {.id = "device.notifications",
         .title = GS_TRANSLATE_NOOP("Commands", "Pull down the notification shade"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "notifications notification shade status bar pull down drawer"),
         .icon = "shade-open",
         .enabled = panels,
         .run = [&host](const DeviceFacts& facts) {
             host.showSystemPanel(facts.serial, protocol::SystemPanelAction::ExpandNotifications);
         }});
    add(table, host,
        {.id = "device.quickSettings",
         .title = GS_TRANSLATE_NOOP("Commands", "Quick settings"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "quick settings tiles toggles wifi bluetooth status bar pull down"),
         .icon = "shade-tiles",
         .enabled = panels,
         .run = [&host](const DeviceFacts& facts) {
             host.showSystemPanel(facts.serial, protocol::SystemPanelAction::ExpandQuickSettings);
         }});
    add(table, host,
        {.id = "device.collapsePanels",
         .title = GS_TRANSLATE_NOOP("Commands", "Close the notification shade"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "collapse close hide notification shade quick settings status bar"),
         .icon = "shade-close",
         .enabled = panels,
         .run = [&host](const DeviceFacts& facts) {
             host.showSystemPanel(facts.serial, protocol::SystemPanelAction::Collapse);
         }});
    add(table, host,
        {.id = "device.diagnostics",
         .title = GS_TRANSLATE_NOOP("Commands", "Diagnostics overlay"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "statistics latency frame rate"),
         .icon = "activity",
         .enabled = connected,
         .checked = [](const DeviceFacts& facts) { return facts.diagnostics; },
         .run = [&host](const DeviceFacts& facts) { host.setDiagnostics(facts.serial, !facts.diagnostics); }});
    add(table, host,
        {.id = "device.fullscreen",
         .title = GS_TRANSLATE_NOOP("Commands", "Full screen"),
         .checkedTitle = GS_TRANSLATE_NOOP("Commands", "Exit full screen"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "fullscreen maximize window"),
         .icon = "maximize-2",
         .scope = CommandScope::Mirror,
         .shortcut = QKeyCombination(Qt::Key_F11),
         .enabled = connected,
         .checked = [](const DeviceFacts& facts) { return facts.fullScreen; },
         .run = [&host](const DeviceFacts& facts) { host.setFullScreen(facts.serial, !facts.fullScreen); }});
    add(table, host,
        {.id = "device.mini",
         .title = GS_TRANSLATE_NOOP("Commands", "Shrink to a mini window on top"),
         .checkedTitle = GS_TRANSLATE_NOOP("Commands", "Restore from the mini window"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "mini small window on top picture in picture corner idle afk"),
         .icon = "picture-in-picture-2",
         .scope = CommandScope::Mirror,
         .enabled = [](const DeviceFacts& facts) { return facts.connected && !facts.editing; },
         .checked = [](const DeviceFacts& facts) { return facts.mini; },
         .run = [&host](const DeviceFacts& facts) { host.setMini(facts.serial, !facts.mini); }});
    add(table, host,
        {.id = "device.editKeymap",
         .title = GS_TRANSLATE_NOOP("Commands", "Edit key mapping"),
         .checkedTitle = GS_TRANSLATE_NOOP("Commands", "Save and finish editing"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "edit keymap keys profile editor layout controls"),
         .icon = "sliders-horizontal",
         .scope = CommandScope::Mirror,
         .enabled = [](const DeviceFacts& facts) { return facts.connected && facts.hasProfile && !facts.mini; },
         .checked = [](const DeviceFacts& facts) { return facts.editing; },
         .run = [&host](const DeviceFacts& facts) { host.setEditing(facts.serial, !facts.editing); }});
    const Predicate editing = [](const DeviceFacts& facts) { return facts.editing; };
    const Predicate arranging = [](const DeviceFacts& facts) { return facts.editing && !facts.trying && !facts.logic; };
    const Predicate undoable = [](const DeviceFacts& facts) { return facts.editing && !facts.trying; };
    add(table, host,
        {.id = "editor.undo",
         .title = GS_TRANSLATE_NOOP("Commands", "Undo the last edit"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "undo back edit keymap editor"),
         .icon = "undo-2",
         .scope = CommandScope::Mirror,
         .shortcut = QKeyCombination(Qt::ControlModifier, Qt::Key_Z),
         .enabled = undoable,
         .run = [&host](const DeviceFacts& facts) { host.editorAction(facts.serial, QStringLiteral("undo")); }});
    add(table, host,
        {.id = "editor.redo",
         .title = GS_TRANSLATE_NOOP("Commands", "Redo the edit"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "redo again edit keymap editor"),
         .icon = "redo-2",
         .scope = CommandScope::Mirror,
         .shortcut = QKeyCombination(Qt::ControlModifier, Qt::Key_Y),
         .enabled = undoable,
         .run = [&host](const DeviceFacts& facts) { host.editorAction(facts.serial, QStringLiteral("redo")); }});
    add(table, host,
        {.id = "editor.delete",
         .title = GS_TRANSLATE_NOOP("Commands", "Delete the selected keys"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "delete remove keys controls keymap editor"),
         .icon = "trash-2",
         .scope = CommandScope::Mirror,
         .shortcut = QKeyCombination(Qt::Key_Delete),
         .enabled = arranging,
         .run = [&host](const DeviceFacts& facts) { host.editorAction(facts.serial, QStringLiteral("delete")); }});
    add(table, host,
        {.id = "editor.duplicate",
         .title = GS_TRANSLATE_NOOP("Commands", "Duplicate the selected keys"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "duplicate copy clone keys controls keymap editor"),
         .icon = "copy",
         .scope = CommandScope::Mirror,
         .shortcut = QKeyCombination(Qt::ControlModifier, Qt::Key_D),
         .enabled = arranging,
         .run = [&host](const DeviceFacts& facts) { host.editorAction(facts.serial, QStringLiteral("duplicate")); }});
    add(table, host,
        {.id = "editor.selectAll",
         .title = GS_TRANSLATE_NOOP("Commands", "Select every key"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "select all keys controls keymap editor"),
         .scope = CommandScope::Mirror,
         .shortcut = QKeyCombination(Qt::ControlModifier, Qt::Key_A),
         .enabled = arranging,
         .run = [&host](const DeviceFacts& facts) { host.editorAction(facts.serial, QStringLiteral("selectAll")); }});
    add(table, host,
        {.id = "editor.save",
         .title = GS_TRANSLATE_NOOP("Commands", "Save the profile being edited"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "save store keymap profile editor"),
         .scope = CommandScope::Mirror,
         .shortcut = QKeyCombination(Qt::ControlModifier, Qt::Key_S),
         .enabled = editing,
         .run = [&host](const DeviceFacts& facts) { host.editorAction(facts.serial, QStringLiteral("save")); }});
    add(table, host,
        {.id = "editor.playtest",
         .title = GS_TRANSLATE_NOOP("Commands", "Try the key mapping"),
         .checkedTitle = GS_TRANSLATE_NOOP("Commands", "Back to editing the key mapping"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "try test play preview keymap editor"),
         .icon = "gamepad-2",
         .scope = CommandScope::Mirror,
         .enabled = editing,
         .checked = [](const DeviceFacts& facts) { return facts.trying; },
         .run = [&host](const DeviceFacts& facts) {
             host.editorAction(facts.serial, facts.trying ? QStringLiteral("edit") : QStringLiteral("try"));
         }});
    add(table, host,
        {.id = "device.measureLatency",
         .title = GS_TRANSLATE_NOOP("Commands", "Measure the real latency"),
         .checkedTitle = GS_TRANSLATE_NOOP("Commands", "Stop measuring the latency"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "latency delay lag measure test touch dots real response"),
         .icon = "gauge",
         .scope = CommandScope::Mirror,
         .enabled = [](const DeviceFacts& facts) { return facts.connected && facts.latencyTest && !facts.editing; },
         .checked = [](const DeviceFacts& facts) { return facts.measuringLatency; },
         .run = [&host](const DeviceFacts& facts) { host.setMeasuringLatency(facts.serial, !facts.measuringLatency); }});
    add(table, host,
        {.id = "mirror.record",
         .title = GS_TRANSLATE_NOOP("Commands", "Record"),
         .checkedTitle = GS_TRANSLATE_NOOP("Commands", "Stop recording"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "record recording capture video sound clip save mp4 replay test logic"),
         .icon = "camera",
         .scope = CommandScope::Mirror,
         .enabled = [](const DeviceFacts& facts) { return facts.connected && !facts.noPicture; },
         .checked = [](const DeviceFacts& facts) { return facts.recording; },
         .run = [&host](const DeviceFacts& facts) { host.setRecording(facts.serial, !facts.recording); }});
    add(table, host,
        {.id = "device.toolbar",
         .title = GS_TRANSLATE_NOOP("Commands", "Show or hide the toolbar"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "toolbar title bar buttons show hide"),
         .icon = "panel-top",
         .scope = CommandScope::Mirror,
         .enabled = connected,
         .run = [&host](const DeviceFacts& facts) { host.toggleToolbar(facts.serial); }});
    add(table, host,
        {.id = "device.keymap",
         .title = GS_TRANSLATE_NOOP("Commands", "Turn on key mapping"),
         .checkedTitle = GS_TRANSLATE_NOOP("Commands", "Turn off key mapping"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "keymap mapping profile keys play"),
         .icon = "keyboard",
         .scope = CommandScope::Mirror,
         .enabled = [](const DeviceFacts& facts) { return facts.connected && facts.hasProfile; },
         .checked = [](const DeviceFacts& facts) { return facts.keymap; },
         .run = [&host](const DeviceFacts& facts) { host.setKeymap(facts.serial, !facts.keymap); }});
    add(table, host,
        {.id = "device.cameraLock",
         .title = GS_TRANSLATE_NOOP("Commands", "Lock the camera"),
         .checkedTitle = GS_TRANSLATE_NOOP("Commands", "Release the camera"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "camera view aim mouse capture lock"),
         .icon = "gamepad-2",
         .scope = CommandScope::Mirror,
         .enabled = [](const DeviceFacts& facts) { return facts.connected && facts.keymap && facts.hasProfile; },
         .checked = [](const DeviceFacts& facts) { return facts.cameraLocked; },
         .run = [&host](const DeviceFacts& facts) { host.setCameraLocked(facts.serial, !facts.cameraLocked); }});
    add(table, host,
        {.id = "device.keyHints",
         .title = GS_TRANSLATE_NOOP("Commands", "Show key hints"),
         .checkedTitle = GS_TRANSLATE_NOOP("Commands", "Hide key hints"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "keymap hints overlay labels keys"),
         .icon = "eye",
         .scope = CommandScope::Mirror,
         .enabled = [](const DeviceFacts& facts) { return facts.connected && facts.hasProfile; },
         .checked = [](const DeviceFacts& facts) { return facts.keyHints; },
         .run = [&host](const DeviceFacts& facts) { host.setKeyHints(facts.serial, !facts.keyHints); }});
    const auto canSwitchProfile = [](const DeviceFacts& facts) { return facts.connected && facts.profiles > 0; };
    add(table, host,
        {.id = "device.profile.next",
         .title = GS_TRANSLATE_NOOP("Commands", "Next profile"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "keymap profile switch cycle game"),
         .icon = "chevron-right",
         .scope = CommandScope::Mirror,
         .enabled = canSwitchProfile,
         .run = [&host](const DeviceFacts& facts) { host.stepProfile(facts.serial, 1); }});
    add(table, host,
        {.id = "device.profile.previous",
         .title = GS_TRANSLATE_NOOP("Commands", "Previous profile"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "keymap profile switch cycle game back"),
         .icon = "arrow-left",
         .scope = CommandScope::Mirror,
         .enabled = canSwitchProfile,
         .run = [&host](const DeviceFacts& facts) { host.stepProfile(facts.serial, -1); }});
    add(table, host,
        {.id = "device.endWireless",
         .title = GS_TRANSLATE_NOOP("Commands", "End wireless debugging connection"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "wifi disconnect adb"),
         .icon = "x",
         .enabled = [](const DeviceFacts& facts) { return facts.wireless; },
         .run = [&host](const DeviceFacts& facts) { host.endWireless(facts.serial); }});
    add(table, host,
        {.id = "device.apps",
         .title = GS_TRANSLATE_NOOP("Commands", "Apps"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "applications games launch start open independent display"),
         .icon = "layout-grid",
         .enabled = [](const DeviceFacts& facts) { return facts.apps; },
         .run = [request](const DeviceFacts& facts) { request(QStringLiteral("device.apps"), facts.serial); }});
    add(table, host,
        {.id = "device.files",
         .title = GS_TRANSLATE_NOOP("Commands", "Files"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "files folders storage upload download copy transfer sdcard"),
         .icon = "folder-open",
         .enabled = [](const DeviceFacts& facts) { return facts.files; },
         .run = [request](const DeviceFacts& facts) { request(QStringLiteral("device.files"), facts.serial); }});
    add(table, host,
        {.id = "device.settings",
         .title = GS_TRANSLATE_NOOP("Commands", "Device settings"),
         .keywords = GS_TRANSLATE_NOOP("Commands", "options preferences override"),
         .icon = "sliders-horizontal",
         .enabled = [](const DeviceFacts& facts) { return !facts.deviceId.isEmpty(); },
         .run = [request](const DeviceFacts& facts) { request(QStringLiteral("device.settings"), facts.serial); }});
}

void addHubCommands(CommandTable& table, DeviceCommandHost& host, const UiRequest& request)
{
    addUi(table, request, "hub.devices", GS_TRANSLATE_NOOP("Commands", "Go to devices"),
          GS_TRANSLATE_NOOP("Commands", "phones list page"), "smartphone");
    addUi(table, request, "hub.profiles", GS_TRANSLATE_NOOP("Commands", "Go to profiles"),
          GS_TRANSLATE_NOOP("Commands", "key mapping page"), "keyboard");
    addUi(table, request, "hub.tasks", GS_TRANSLATE_NOOP("Commands", "Go to tasks"),
          GS_TRANSLATE_NOOP("Commands", "tasks automation schedule page"), "bot");
    addUi(table, request, "hub.extensions", GS_TRANSLATE_NOOP("Commands", "Go to extensions"),
          GS_TRANSLATE_NOOP("Commands", "extensions page plugins add-ons"), "puzzle", CommandScope::Global, {},
          CommandTarget::Topic);
    addUi(table, request, "hub.settings", GS_TRANSLATE_NOOP("Commands", "Open settings"),
          GS_TRANSLATE_NOOP("Commands", "options preferences page"), "settings-2");
    addUi(table, request, "settings.search", GS_TRANSLATE_NOOP("Commands", "Search settings"),
          GS_TRANSLATE_NOOP("Commands", "find options"), "search", CommandScope::Hub,
          QKeyCombination(Qt::ControlModifier, Qt::Key_F));
    addUi(table, request, "wireless.pair", GS_TRANSLATE_NOOP("Commands", "Pair a phone over WiFi"),
          GS_TRANSLATE_NOOP("Commands", "wireless debugging pairing code qr"), "wifi");
    addUi(table, request, "app.about", GS_TRANSLATE_NOOP("Commands", "About GameScrcpy"),
          GS_TRANSLATE_NOOP("Commands", "help version license"), "circle-help");
    addUi(table, request, "app.sponsor", GS_TRANSLATE_NOOP("Commands", "Sponsor…"),
          GS_TRANSLATE_NOOP("Commands", "sponsor donate support afdian thanks"), "heart");
    addUi(table, request, "app.help", GS_TRANSLATE_NOOP("Commands", "Help"),
          GS_TRANSLATE_NOOP("Commands", "manual guide articles questions troubleshooting faq script api reference"),
          "circle-help", CommandScope::Global, QKeyCombination(Qt::Key_F1), CommandTarget::Topic);
    addUi(table, request, "app.guide", GS_TRANSLATE_NOOP("Commands", "Getting started"),
          GS_TRANSLATE_NOOP("Commands", "guide welcome tutorial help usb debugging developer options first run"),
          "smartphone");
    addUi(table, request, "app.commandPalette", GS_TRANSLATE_NOOP("Commands", "Command palette"),
          GS_TRANSLATE_NOOP("Commands", "commands search run actions"), "search", CommandScope::Global,
          QKeyCombination(Qt::ControlModifier, Qt::Key_K));
    addUi(table, request, "app.assistant", GS_TRANSLATE_NOOP("Commands", "AI assistant"),
          GS_TRANSLATE_NOOP("Commands", "ai assistant chat ask question help bubble"), "sparkles",
          CommandScope::Global, QKeyCombination(Qt::ControlModifier, Qt::Key_J));

    const auto mirroring = [&host] {
        const std::vector<DeviceFacts> facts = host.devices();
        return std::ranges::count_if(facts, &DeviceFacts::connected);
    };
    Command grid;
    grid.id = QStringLiteral("devices.grid");
    grid.title = GS_TRANSLATE_NOOP("Commands", "View all phones in a grid");
    grid.keywords = GS_TRANSLATE_NOOP("Commands", "grid wall many multiple phones devices overview together");
    grid.icon = QStringLiteral("layout-grid");
    grid.enabled = [mirroring](const QString&) { return mirroring() >= 2; };
    grid.run = [request](const QString&) { request(QStringLiteral("devices.grid"), {}); };
    [[maybe_unused]] const bool gridAdded = table.add(std::move(grid));
    Q_ASSERT_X(gridAdded, "addHubCommands", "invalid or duplicate command");
    Command tile;
    tile.id = QStringLiteral("devices.tile");
    tile.title = GS_TRANSLATE_NOOP("Commands", "Tile all mirror windows");
    tile.keywords = GS_TRANSLATE_NOOP("Commands", "tile arrange windows side by side layout screen");
    tile.icon = QStringLiteral("windows-tile");
    tile.enabled = [mirroring](const QString&) { return mirroring() >= 1; };
    tile.run = [request](const QString&) { request(QStringLiteral("devices.tile"), {}); };
    [[maybe_unused]] const bool tileAdded = table.add(std::move(tile));
    Q_ASSERT_X(tileAdded, "addHubCommands", "invalid or duplicate command");

    Command retry;
    retry.id = QStringLiteral("adb.retry");
    retry.title = GS_TRANSLATE_NOOP("Commands", "Start adb again");
    retry.keywords = GS_TRANSLATE_NOOP("Commands", "restart server devices");
    retry.enabled = [&host](const QString&) { return host.canRetryAdb(); };
    retry.run = [&host](const QString&) { host.retryAdb(); };
    [[maybe_unused]] const bool added = table.add(std::move(retry));
    Q_ASSERT_X(added, "addHubCommands", "invalid or duplicate command");
}

bool passesTyping(QStringView command) noexcept
{
    return command == u"app.commandPalette" || command == u"device.keymap" || command == u"device.cameraLock";
}

}
