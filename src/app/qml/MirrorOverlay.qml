pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Item {
    id: overlay

    required property string serial
    property real framesPerSecond: -1
    property real latencyMs: -1
    property string transport: ""
    property string statusProblem: ""
    property string extensionNotice: ""
    readonly property string capsuleProblem: statusProblem !== "" ? statusProblem : extensionNotice
    property real volume: 1
    property bool fullScreen: false
    property bool mini: false
    property bool noPicture: false
    property int controlTurn: 0
    property bool captureCard: false
    property bool manualCrop: false
    property bool cropEditing: false
    property rect cropBox: Qt.rect(0, 0, 1, 1)
    property bool toolbarPinned: false
    property bool titleFixed: true
    readonly property int fixedTitleHeight: titleBar.titleHeight
    property int reservedTop: 0
    readonly property string toolbarMode: AppSettings.values["display.toolbar"] ?? "pinned"
    readonly property bool cameraGlow: AppSettings.values["display.cameraGlow"] ?? true
    property bool closeChosen: false
    readonly property real captionsWidth: fullScreen ? 0 : captions.width
    readonly property real titleStatusWidth: (titleStatus.visible ? titleStatus.width + Theme.space.xs : 0)
                                             + (titleWidgets.visible ? titleWidgets.width + Theme.space.xs : 0)
    readonly property bool pinnedTwoRows: width < toolbar.implicitWidth + captionsWidth + titleStatusWidth
                                                  + Theme.space.x6l + 2 * Theme.space.lg
    readonly property int pinnedBarHeight: pinnedTwoRows ? titleBar.titleHeight + Theme.size.toolbar
                                                         : Theme.size.toolbar
    property bool keymap: true
    property bool cameraLocked: false
    property bool crosshair: false
    property bool textInput: false
    property string profileName
    property string profileKey
    property string profileId
    property string foregroundApp
    property string startOffer
    readonly property string startGame: internal.startOpened ? foregroundApp : startOffer
    readonly property bool startShown: (internal.startOpened || startOffer !== "") && !editing && !mini && !textInput
    property var keyHints: []
    property var actionWheel: ({})
    property var liveControls: []
    property var aimingControls: []
    property var latencyTest: ({})
    property var phoneStatus: ({})
    readonly property bool phoneStatusOn: phoneStatus.on === true
    property var recording: ({})
    readonly property bool recordingOn: recording.on === true
    property real recordingNow: 0
    readonly property string recordingText: qsTr("Recording %1").arg(overlay.clockText(overlay.recordingNow
                                                                                       - (recording.startedMs ?? 0)))
    readonly property string recordingNote: [recording.sound === true ? qsTr("Picture · Sound · Operations")
                                                                      : qsTr("Picture · Operations"),
                                             qsTr("Typing: length only")].join(" / ")
    property ProfileEditor editor: null
    readonly property bool editing: editor !== null
    readonly property bool trying: editor !== null && editor.trying
    readonly property bool playing: !editing || trying
    readonly property int editingTop: titleBar.titleHeight + Theme.size.workbenchBar
    readonly property int editingRight: Theme.size.workbenchSidebar
    readonly property int editingBottom: keymapEditor.toolsRoom
    readonly property var taskRuns: DeviceManager.taskRuns.filter(run => run.serial === overlay.serial)
    readonly property string taskText: taskRuns.length === 0 ? ""
                                       : taskRuns.length === 1 ? qsTr("Task: %1").arg(taskRuns[0].name)
                                                               : qsTr("%n task(s) running", "", taskRuns.length)
    readonly property int logicWidth: Theme.size.workbenchLeft + Theme.size.workbenchCode + Theme.size.workbenchSidebar
    readonly property int logicHeight: editingTop + Theme.size.tab + Math.round(Theme.size.workbenchCode * 5 / 8)
                                       + Theme.size.workbenchBottom
    property rect pictureRect
    property bool firstRun: !AppSettings.mirrorToolbarHintShown
    readonly property string capsuleMode: AppSettings.values["display.statusCapsule"] ?? "auto"
    readonly property bool capsuleShown: capsuleMode === "always"
                                         || (capsuleMode === "auto" && (capsuleProblem !== "" || capsuleFlash.running
                                                                        || recordingOn))
    readonly property bool hintsShown: keymap && !textInput && keyHints.length > 0
                                       && (hintsRevealed
                                           || (hintsForced >= 0 ? hintsForced === 1
                                                                : AppSettings.values["keymap.hints"] !== false))
    property int hintsForced: -1
    property real hintOpacityForced: -1
    property bool hintsRevealed: false
    property string syncRole
    property int syncTone: -1
    property int syncCount: 0
    property string syncLeader
    readonly property var syncHues: ["azure", "teal", "rose", "lime", "amber", "orange"].filter(
                                        hue => Theme.toneHues[hue] !== Theme.accentName)
    readonly property string syncHue: syncRole !== "" && syncTone >= 0 && syncHues.length > 0
                                      ? syncHues[syncTone % syncHues.length] : ""
    property var companionQr: ({})
    readonly property var companionHintLook: ({ shown: overlay.hintsShown,
                                                faded: !keyHintLayer.introducing && !keyHintLayer.revealed,
                                                opacity: keyHintLayer.restingOpacity })
    property bool rightClaimed: false
    readonly property var extensionPanels: ExtensionPanels.panels.filter(entry => entry.device === overlay.serial)
    readonly property var sidePanels: overlay.extensionPanels.filter(entry => entry.where === "mirror")
    readonly property var statusWidgets: overlay.extensionPanels.filter(entry => entry.where === "status")
    readonly property string statusWidgetKeys: overlay.statusWidgets.map(entry => entry.key).join(" ")
    readonly property var extensionMenus: ExtensionPanels.menus.filter(group => group.device === overlay.serial
                                                                                && group.where === "menu")
    readonly property var toolbarButtons: overlay.extensionPanels.filter(entry => entry.where === "toolbar")

    signal pictureFocusWanted()

    function openStart() {
        internal.startOpened = true
        internal.focusStart = true
    }

    function closeStart() {
        if (startOffer !== "" && startPanel.game === startOffer)
            DeviceManager.closeStart(serial, startOffer)
        internal.startOpened = false
    }

    function importForStart(game: string, files: var) {
        overlay.importStart(game, files, false)
    }

    function importStart(game: string, files: var, allowLogic: bool) {
        const before = ProfileLibrary.entries.map(entry => entry.id)
        const result = ProfileLibrary.importFiles(files, allowLogic)
        if (result.failed.length > 0) {
            toast(qsTr("Could not import %1: %2").arg(result.failed[0].file).arg(result.failed[0].reason),
                  "triangle-alert")
            return
        }
        if (result.logic.length > 0) {
            logicImport.game = game
            logicImport.ask(result.logic)
            return
        }
        const added = ProfileLibrary.entries.filter(entry => !before.includes(entry.id) && entry.error === "")
        if (added.length !== 1) {
            if (result.imported.length > 0)
                toast(qsTr("Imported %n profile(s)", "", result.imported.length), "check")
            return
        }
        if (added[0].app === "" && game !== "")
            ProfileLibrary.setApp(added[0].id, game)
        DeviceManager.useProfile(serial, added[0].id)
        internal.startOpened = false
    }

    function prompt(request, shown) {
        internal.prompts.push({ request: request, shown: shown })
        overlay.nextPrompt()
    }

    function withdrawPrompt(request) {
        internal.prompts = internal.prompts.filter(waiting => waiting.request !== request)
        if (scriptPrompt.visible && scriptPrompt.request === request) {
            scriptPrompt.visible = false
            scriptPrompt.request = ""
            overlay.nextPrompt()
        }
    }

    function nextPrompt() {
        if (scriptPrompt.visible || internal.prompts.length === 0) {
            DeviceManager.setPrompting(overlay.serial, scriptPrompt.visible || extensionSide.typing)
            return
        }
        const waiting = internal.prompts.shift()
        scriptPrompt.open(waiting.request, waiting.shown)
        DeviceManager.setPrompting(overlay.serial, true)
    }

    function draw(id, shown) {
        scriptDrawings.draw(id, shown)
    }

    function erase(id) {
        scriptDrawings.erase(id)
    }

    function clearDrawings() {
        scriptDrawings.clear()
    }

    function setProgress(source, title, value) {
        scriptDrawings.setProgress(source, title, value)
    }

    function clearProgress(source) {
        scriptDrawings.clearProgress(source)
    }

    function toast(text, icon, action, secondAction) {
        if (notice.visible)
            notice.close()
        notice.iconName = icon ?? ""
        notice.actionText = action ?? ""
        notice.secondActionText = secondAction ?? ""
        notice.show(text, "info")
    }

    function showRecording(info) {
        recordingDialog.show(info)
    }

    function clockText(milliseconds: real): string {
        const seconds = Math.max(0, Math.floor(milliseconds / 1000))
        const hours = Math.floor(seconds / 3600)
        const pad = value => (value < 10 ? "0" : "") + value
        return (hours > 0 ? hours + ":" : "") + pad(Math.floor(seconds / 60) % 60) + ":" + pad(seconds % 60)
    }

    function revealToolbar() {
        if (!editing)
            toolbar.reveal()
    }

    function revealFromTitle() {
        if (!editing && !mini && !cameraLocked && !toolbarPinned && toolbarMode === "dwell")
            toolbar.reveal()
    }

    function confirmClose() {
        closeEditing.open()
    }

    function closeRequested() {
        const choice = String(AppSettings.values["display.closeWhileEditing"] ?? "ask")
        if (choice === "exitEditing") {
            DeviceManager.finishEditing(overlay.serial, true)
        } else if (choice === "closeWindow") {
            overlay.closeAnyway()
        } else {
            rememberClose.checked = false
            closeChoice.open()
        }
    }

    function closeAnyway() {
        if (overlay.editor !== null && overlay.editor.dirty) {
            closeEditing.open()
            return
        }
        overlay.closeChosen = true
        overlay.closeWindow()
    }

    function closeWindow() {
        Window.window.close()
    }

    function toggleToolbar() {
        if (toolbar.shown)
            toolbar.conceal()
        else
            toolbar.reveal()
    }

    onToolbarPinnedChanged: {
        if (toolbarPinned)
            toolbar.reveal()
    }

    onEditingChanged: {
        overlay.closeChosen = false
        if (overlay.editing)
            Qt.callLater(tour.cancel)
    }
    onFullScreenChanged: {
        if (fullScreen) {
            const keys = fullScreenCommand.shortcut !== "" ? fullScreenCommand.shortcut : "F11"
            toast(qsTr("Press %1 to exit full screen").arg(keys), "maximize-2")
        }
    }
    onForegroundAppChanged: internal.startOpened = false
    onTransportChanged: capsuleFlash.restart()
    onProfileNameChanged: capsuleFlash.restart()
    onCameraLockedChanged: capsuleFlash.restart()
    onStatusProblemChanged: capsuleFlash.restart()
    onStatusWidgetKeysChanged: capsuleFlash.restart()
    onExtensionNoticeChanged: capsuleFlash.restart()
    onFirstRunChanged: internal.hintFirstRun()
    onMiniChanged: {
        if (overlay.mini)
            Qt.callLater(tour.cancel)
    }
    Window.onWindowChanged: internal.attachChrome()
    Window.onVisibilityChanged: internal.hintFirstRun()
    Component.onCompleted: {
        internal.attachChrome()
        internal.hintFirstRun()
        if (toolbarPinned)
            toolbar.reveal()
    }

    QtObject {
        id: internal

        property bool hinted: false
        property bool chromeAttached: false
        property var prompts: []
        property real fullToolbarWidth: 0
        property real compactToolbarWidth: 0
        property bool startOpened: false
        property bool focusStart: false

        function hintFirstRun() {
            if (!overlay.firstRun || hinted || overlay.Window.visibility === Window.Hidden
                    || overlay.toolbarMode !== "dwell")
                return
            hinted = true
            overlay.toast(qsTr("Rest the pointer at the top to show the toolbar"), "info")
            AppSettings.markMirrorToolbarHintShown()
        }

        function attachChrome() {
            if (chromeAttached || overlay.Window.window === null)
                return
            chrome.window = overlay.Window.window
            chromeAttached = chrome.attach()
            if (chromeAttached) {
                chrome.setHitTestVisible(toolbar, true)
                chrome.setHitTestVisible(titleStatus, true)
            }
        }
    }

    WindowChrome {
        id: chrome

        titleBar: titleBar
        minimizeButton: minimizeButton
        maximizeButton: maximizeButton
        closeButton: closeButton
        darkMode: Theme.dark
    }

    Binding {
        target: overlay.Window.window
        property: "color"
        value: Theme.colors.bgBase
        when: overlay.Window.window !== null
    }

    Timer {
        id: capsuleFlash

        interval: Theme.motion.capsuleStay
    }

    Timer {
        objectName: "recordingClock"
        interval: 1000
        repeat: true
        triggeredOnStart: true
        running: overlay.recordingOn
        onTriggered: overlay.recordingNow = Date.now()
    }

    component DeviceCommand: CommandState {
        target: overlay.serial
    }

    component MenuLabel: Item {
        property alias text: label.text

        implicitWidth: label.implicitWidth + 2 * (Theme.space.sm + Theme.space.xxs)
        implicitHeight: Theme.size.pill + Theme.space.xs
        height: visible ? implicitHeight : 0

        Text {
            id: label

            x: Theme.space.sm + Theme.space.xxs
            anchors.bottom: parent.bottom
            textFormat: Text.PlainText
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
            font.weight: Font.DemiBold
        }
    }

    component CapsuleChoice: MenuItem {
        required property string mode

        choice: true
        chosen: overlay.capsuleMode === mode
        onTriggered: AppSettings.setValue("display.statusCapsule", mode)
    }

    component CapsuleMenu: Menu {
        MenuLabel { text: qsTr("Status capsule") }
        CapsuleChoice { mode: "auto"; text: qsTr("Automatic") }
        CapsuleChoice { mode: "always"; text: qsTr("Always shown") }
        CapsuleChoice { mode: "hidden"; text: qsTr("Hidden") }
        MenuSeparator {}
        CommandMenuItem { commandState: diagnosticsCommand }
        CommandMenuItem { commandState: latencyCommand }
    }

    component TurnChoice: MenuItem {
        required property int turn

        objectName: "turnChoice"
        height: visible ? implicitHeight : 0
        choice: true
        chosen: overlay.controlTurn === turn
        onTriggered: DeviceManager.setControlOrientation(overlay.serial, turn)
    }

    component CommandButton: ToolbarButton {
        required property CommandState commandState
        property string fallbackIcon
        property string checkedIcon

        iconName: {
            if (commandState.checked && checkedIcon !== "")
                return checkedIcon
            return commandState.icon !== "" ? commandState.icon : fallbackIcon
        }
        text: commandState.title
        shortcut: commandState.shortcut
        enabled: commandState.enabled
        selected: commandState.checked
        onClicked: commandState.trigger()
    }

    DeviceCommand { id: keymapCommand; command: "device.keymap" }
    DeviceCommand { id: hintsCommand; command: "device.keyHints" }
    DeviceCommand { id: backCommand; command: "device.back" }
    DeviceCommand { id: homeCommand; command: "device.home" }
    DeviceCommand { id: appSwitchCommand; command: "device.appSwitch" }
    DeviceCommand { id: followCommand; command: "device.orientation.follow" }
    DeviceCommand { id: portraitCommand; command: "device.orientation.portrait" }
    DeviceCommand { id: landscapeCommand; command: "device.orientation.landscape" }
    DeviceCommand { id: screenCommand; command: "device.screenOff" }
    DeviceCommand { id: muteCommand; command: "device.mute" }
    DeviceCommand { id: diagnosticsCommand; command: "device.diagnostics" }
    DeviceCommand { id: fullScreenCommand; command: "device.fullscreen" }
    DeviceCommand { id: miniCommand; command: "device.mini" }
    DeviceCommand { id: latencyCommand; command: "device.measureLatency" }
    DeviceCommand { id: recordCommand; command: "mirror.record" }
    DeviceCommand { id: editKeymapCommand; command: "device.editKeymap" }
    DeviceCommand { id: settingsCommand; command: "device.settings" }
    DeviceCommand { id: disconnectCommand; command: "device.disconnect" }
    DeviceCommand { id: filesCommand; command: "device.files" }
    DeviceCommand { id: notificationsCommand; command: "device.notifications" }
    DeviceCommand { id: quickSettingsCommand; command: "device.quickSettings" }
    DeviceCommand { id: collapsePanelsCommand; command: "device.collapsePanels" }
    CommandState { id: profilesCommand; command: "hub.profiles" }
    CommandState { id: tasksPageCommand; command: "hub.tasks" }

    Rectangle {
        objectName: "noPictureCanvas"
        visible: overlay.noPicture && overlay.pictureRect.width > 0 && overlay.pictureRect.height > 0
                 && !overlay.editing
        x: overlay.pictureRect.x
        y: overlay.pictureRect.y
        width: overlay.pictureRect.width
        height: overlay.pictureRect.height
        color: Theme.colors.bgSunken
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeSubtle

        Column {
            anchors.centerIn: parent
            width: parent.width - 2 * Theme.space.xl
            spacing: Theme.space.sm
            visible: !overlay.mini

            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: "smartphone"
                size: Theme.size.iconLarge
                color: Theme.colors.textTertiary
            }

            Text {
                objectName: "noPictureTitle"
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                textFormat: Text.PlainText
                text: qsTr("No picture · Watch the phone")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.subtitle.size
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }

            Text {
                objectName: "noPictureLimits"
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                textFormat: Text.PlainText
                text: qsTr("Not available without a picture: %1")
                      .arg(DeviceManager.noPictureLimits(overlay.serial).join(qsTr(", ")))
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
                wrapMode: Text.Wrap
            }

            Item {
                visible: companionQrBlock.shown
                width: parent.width
                height: Theme.space.md
            }

            CompanionQrBlock {
                id: companionQrBlock

                width: parent.width
                qr: overlay.companionQr
            }
        }
    }

    ScriptDrawings {
        id: scriptDrawings

        objectName: "scriptDrawings"
        anchors.fill: parent
        picture: overlay.pictureRect
        visible: !overlay.editing && !overlay.mini
    }

    KeyHintLayer {
        id: keyHintLayer

        anchors.fill: parent
        hints: overlay.keyHints
        live: overlay.liveControls
        aiming: overlay.aimingControls
        picture: overlay.pictureRect
        shown: overlay.hintsShown && !overlay.mini && overlay.playing
        restingOpacity: overlay.hintOpacityForced >= 0 ? overlay.hintOpacityForced
                                                        : (AppSettings.values["keymap.hintOpacity"] ?? 60) / 100
        revealed: overlay.hintsRevealed
        intro: AppSettings.values["keymap.hintIntro"] !== false
        introDuration: AppSettings.values["keymap.hintIntroMs"] ?? Theme.motion.hintIntro
        introKey: overlay.profileKey
    }

    ActionWheelView {
        wheel: overlay.actionWheel
        picture: overlay.pictureRect
        enabled: false
    }

    EdgeGlow {
        anchors.fill: parent
        active: overlay.keymap && overlay.cameraLocked && !overlay.textInput && !overlay.mini && overlay.playing
                && overlay.cameraGlow
    }

    UiContextMenu {
        x: overlay.pictureRect.x
        y: overlay.pictureRect.y
        width: overlay.pictureRect.width
        height: overlay.pictureRect.height
        groups: overlay.extensionMenus
        enabled: overlay.extensionMenus.length > 0 && !overlay.rightClaimed && !overlay.cameraLocked
                 && !overlay.editing && !overlay.mini && !overlay.noPicture && !overlay.cropEditing
    }

    CaptureCropEditor {
        objectName: "captureCropEditor"
        anchors.fill: parent
        visible: overlay.cropEditing && !overlay.mini
        picture: overlay.pictureRect
        box: overlay.cropBox
        onApplied: area => DeviceManager.finishCaptureCrop(overlay.serial, true, area.x, area.y, area.width,
                                                           area.height)
        onCancelled: DeviceManager.finishCaptureCrop(overlay.serial, false, 0, 0, 0, 0)
        onAutomatic: DeviceManager.autoCaptureCrop(overlay.serial)
    }

    Item {
        id: crosshair

        objectName: "crosshair"
        readonly property int gap: Theme.space.xs
        readonly property int arm: Theme.space.sm
        readonly property int thickness: Theme.size.focusRing + 2 * Theme.size.stroke

        visible: overlay.crosshair && overlay.keymap && overlay.cameraLocked && !overlay.textInput && !overlay.mini
                 && overlay.playing
        width: 2 * (gap + arm) + thickness
        height: width
        x: Math.round(overlay.pictureRect.x + (overlay.pictureRect.width - width) / 2)
        y: Math.round(overlay.pictureRect.y + (overlay.pictureRect.height - height) / 2)

        Repeater {
            model: 4

            Rectangle {
                required property int index
                readonly property bool horizontal: index < 2

                width: horizontal ? crosshair.arm : crosshair.thickness
                height: horizontal ? crosshair.thickness : crosshair.arm
                x: horizontal ? (index === 0 ? 0 : crosshair.width - width) : (crosshair.width - width) / 2
                y: horizontal ? (crosshair.height - height) / 2 : (index === 2 ? 0 : crosshair.height - height)
                radius: Theme.size.stroke
                color: Theme.colors.keyHintText
                border.width: Theme.size.stroke
                border.color: Theme.colors.keyHintFill
            }
        }

        Rectangle {
            anchors.centerIn: parent
            width: crosshair.thickness
            height: width
            radius: width / 2
            color: Theme.colors.keyHintText
            border.width: Theme.size.stroke
            border.color: Theme.colors.keyHintFill
        }
    }

    Rectangle {
        id: phoneStatusLine

        objectName: "phoneStatus"
        readonly property string text: overlay.phoneStatus.text ?? ""

        visible: overlay.phoneStatusOn && overlay.phoneStatus.available === true && text !== "" && !overlay.mini
                 && !overlay.editing && !overlay.noPicture
        x: Math.round(overlay.pictureRect.x + overlay.pictureRect.width - width - Theme.space.sm)
        y: Math.round(overlay.pictureRect.y + Theme.space.sm)
        implicitWidth: phoneStatusText.implicitWidth + 2 * Theme.space.sm
        implicitHeight: Theme.size.pill
        radius: height / 2
        color: Theme.reducedTransparency ? Theme.colors.bgElevated : Theme.colors.bgFrosted
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeStrong

        Accessible.role: Accessible.StaticText
        Accessible.name: phoneStatusLine.text

        Text {
            id: phoneStatusText

            objectName: "phoneStatusText"
            anchors.centerIn: parent
            textFormat: Text.PlainText
            text: phoneStatusLine.text
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.caption.size
            font.features: ({ "tnum": 1 })
        }
    }

    UiSidePanel {
        id: extensionSide

        serial: overlay.serial
        panels: overlay.sidePanels
        topInset: overlay.reservedTop
        allowed: !overlay.editing && !overlay.mini && !overlay.cropEditing
        onTypingChanged: overlay.nextPrompt()
        onFocusReleased: overlay.pictureFocusWanted()
    }

    ToolbarRevealArea {
        anchors.fill: parent
        toolbar: toolbar
        zoneTop: titleBar.fixedRow ? titleBar.height : 0
        active: !overlay.cameraLocked && !overlay.toolbarPinned && overlay.toolbarMode === "dwell" && !overlay.mini
                && !overlay.editing
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: overlay.pinnedBarHeight
        visible: overlay.toolbarPinned && !overlay.mini && !overlay.editing
        color: Theme.colors.bgBase

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Theme.size.stroke
            color: Theme.colors.strokeSubtle
        }
    }

    Item {
        id: titleBar

        readonly property bool shown: !overlay.mini && (overlay.editing || overlay.toolbarPinned
                                                        || ((toolbar.shown || overlay.titleFixed) && !overlay.fullScreen))
        readonly property bool fixedRow: overlay.titleFixed && !overlay.toolbarPinned && !overlay.editing
                                         && !overlay.fullScreen
        readonly property bool pinnedRow: overlay.toolbarPinned && !overlay.pinnedTwoRows && !overlay.editing
        readonly property bool statusRow: fixedRow || (overlay.toolbarPinned && !overlay.editing)
        readonly property int titleHeight: 40

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: pinnedRow ? Theme.size.toolbar : titleHeight
        opacity: shown ? 1 : 0
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.motion.fast
                easing.type: Easing.BezierSpline
                easing.bezierCurve: Theme.motion.standard
            }
        }

        Rectangle {
            anchors.fill: parent
            visible: !overlay.toolbarPinned || overlay.editing
            color: overlay.editing || titleBar.fixedRow ? Theme.colors.bgBase
                 : Theme.reducedTransparency ? Theme.colors.bgElevated : Theme.colors.bgFrosted

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: Theme.size.stroke
                visible: titleBar.fixedRow
                color: Theme.colors.strokeSubtle
            }
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.space.lg
            width: Math.max(0, (titleBar.pinnedRow ? toolbar.x : titleWidgets.visible ? titleWidgets.x
                                  : titleStatus.visible ? titleStatus.x : captions.x) - x - Theme.space.md)
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: {
                const title = overlay.Window.window !== null ? overlay.Window.window.title : ""
                if (overlay.editor !== null && overlay.editor.preview)
                    return qsTr("%1 — %2's draft").arg(title).arg(overlay.editor.previewClient)
                return overlay.editing ? qsTr("%1 — Edit key mapping").arg(title) : title
            }
            elide: Text.ElideRight
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.caption.size
            font.weight: Font.Medium
        }

        StatusCapsule {
            id: titleStatus

            objectName: "titleStatus"
            anchors.right: captions.left
            anchors.rightMargin: Theme.space.xs
            anchors.verticalCenter: parent.verticalCenter
            flat: true
            visible: titleBar.statusRow && !overlay.mini && overlay.capsuleMode !== "hidden" && hasContent
            framesPerSecond: overlay.framesPerSecond
            latencyMs: overlay.latencyMs
            transport: overlay.transport
            profileName: overlay.keymap ? overlay.profileName : ""
            task: overlay.taskText
            cameraLocked: overlay.keymap && overlay.cameraLocked
            problem: overlay.capsuleProblem
            recording: overlay.recordingOn
            recordingText: overlay.recordingText
            recordingNote: overlay.recordingNote

            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                acceptedButtons: Qt.LeftButton
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: diagnosticsCommand.trigger()
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: titleStatusMenu.open()
            }

            CapsuleMenu {
                id: titleStatusMenu

                x: titleStatus.width - width
                y: titleStatus.height + Theme.space.xs
            }
        }

        UiStatusRow {
            id: titleWidgets

            objectName: "titleStatusWidgets"
            anchors.right: titleStatus.visible ? titleStatus.left : captions.left
            anchors.rightMargin: Theme.space.xs
            anchors.verticalCenter: parent.verticalCenter
            visible: titleBar.statusRow && !overlay.mini && overlay.capsuleMode !== "hidden"
                     && overlay.statusWidgets.length > 0
            flat: true
            widgets: overlay.statusWidgets
        }

        Row {
            id: captions

            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height
            visible: !overlay.fullScreen

            CaptionButton {
                id: minimizeButton

                glyph: "\uE921"
                onClicked: overlay.Window.window.showMinimized()
            }

            CaptionButton {
                id: maximizeButton

                glyph: overlay.Window.visibility === Window.Maximized ? "\uE923" : "\uE922"
                onClicked: {
                    if (overlay.Window.visibility === Window.Maximized)
                        overlay.Window.window.showNormal()
                    else
                        overlay.Window.window.showMaximized()
                }
            }

            CaptionButton {
                id: closeButton

                glyph: "\uE8BB"
                destructive: true
                onClicked: overlay.Window.window.close()
            }
        }
    }

    FloatingToolbar {
        id: toolbar

        readonly property bool compact: overlay.width < internal.fullToolbarWidth + 2 * Theme.space.sm
        readonly property bool tight: compact && overlay.width < internal.compactToolbarWidth + 2 * Theme.space.sm

        visible: !overlay.mini && !overlay.editing && opacity > 0

        onImplicitWidthChanged: {
            if (!compact)
                internal.fullToolbarWidth = implicitWidth
            else if (!tight)
                internal.compactToolbarWidth = implicitWidth
        }

        x: {
            const centered = Math.max(Theme.space.xs, (overlay.width - width) / 2)
            if (!overlay.toolbarPinned || overlay.pinnedTwoRows)
                return centered
            const beforeCaptions = overlay.width - overlay.captionsWidth - overlay.titleStatusWidth - width
                                   - Theme.space.sm
            return Math.max(Theme.space.x6l, Math.min(centered, beforeCaptions))
        }
        y: {
            if (overlay.toolbarPinned)
                return overlay.pinnedTwoRows ? titleBar.titleHeight : (Theme.size.toolbar - height) / 2
            return overlay.fullScreen ? Theme.space.md : titleBar.height + Theme.space.xs
        }
        flat: overlay.toolbarPinned
        pinned: overlay.toolbarPinned || keymapMenu.visible || rotateMenu.visible || turnMenu.visible
                || volumePanel.visible || moreMenu.visible || tasksMenu.visible || panelsMenu.visible || tour.running

        T.AbstractButton {
            id: syncCapsule

            objectName: "syncCapsule"
            anchors.verticalCenter: parent.verticalCenter
            visible: overlay.syncRole === "master"
            implicitWidth: syncRow.implicitWidth + 2 * Theme.space.md
            implicitHeight: Theme.size.controlSmall
            hoverEnabled: true
            focusPolicy: Qt.TabFocus
            text: qsTr("Syncing · %n phone(s)", "", overlay.syncCount)
            Accessible.name: text
            onClicked: syncDialog.show()

            Tooltip {
                y: syncCapsule.height + Theme.space.xs
                text: qsTr("Change or stop syncing")
                visible: syncCapsule.hovered && !syncCapsule.down
            }

            contentItem: Item {
                Row {
                    id: syncRow

                    anchors.centerIn: parent
                    spacing: Theme.space.xs + Theme.space.xxs

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.size.pillDot
                        height: width
                        radius: width / 2
                        color: Theme.toneInk(overlay.syncHue)
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: syncCapsule.text
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.caption.size
                        font.weight: Font.Medium
                    }
                }
            }

            background: Rectangle {
                id: syncFace

                radius: height / 2
                color: Theme.toneFill(overlay.syncHue)

                StateLayer {
                    anchors.fill: parent
                    radius: syncFace.radius
                    hovered: syncCapsule.hovered
                    pressed: syncCapsule.down
                }

                FocusRing {
                    visible: syncCapsule.visualFocus
                    baseRadius: syncFace.radius
                }
            }
        }

        ToolbarSeparator { visible: syncCapsule.visible }

        CommandButton {
            id: keymapButton

            objectName: "tourKeymap"
            commandState: keymapCommand
            fallbackIcon: "keyboard"
        }

        Item { width: Theme.space.xxs }

        ToolbarButton {
            id: keymapMenuButton

            compact: true
            iconName: "chevron-down"
            text: qsTr("Key mapping options")
            enabled: backCommand.enabled
            selected: keymapMenu.visible
            onClicked: keymapMenu.open()

            Menu {
                id: keymapMenu

                readonly property int firstProfile: 4

                y: keymapMenuButton.height + Theme.space.xs
                onClosed: {
                    if (!internal.focusStart)
                        return
                    internal.focusStart = false
                    if (startPanel.visible)
                        startPanel.forceActiveFocus(Qt.PopupFocusReason)
                }

                MenuItem {
                    enabled: hintsCommand.enabled
                    choice: true
                    chosen: hintsCommand.checked
                    text: qsTr("Show key hints")
                    shortcut: hintsCommand.shortcut
                    onTriggered: hintsCommand.trigger()
                }

                Item {
                    implicitWidth: opacityRow.implicitWidth + 2 * (Theme.space.sm + Theme.space.xxs)
                    implicitHeight: Theme.size.menuItem
                    enabled: hintsCommand.enabled && hintsCommand.checked

                    Row {
                        id: opacityRow

                        anchors.verticalCenter: parent.verticalCenter
                        x: Theme.space.sm + Theme.space.xxs
                        spacing: Theme.space.md

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: qsTr("Opacity")
                            color: parent.enabled ? Theme.colors.textPrimary : Theme.colors.textDisabled
                            font.pixelSize: Theme.type.label.size
                        }

                        Slider {
                            anchors.verticalCenter: parent.verticalCenter
                            implicitWidth: Theme.size.sliderCompact + valueWidth
                            from: 10
                            to: 100
                            stepSize: 5
                            value: AppSettings.values["keymap.hintOpacity"] ?? 60
                            valueText: Math.round(value) + "%"
                            Accessible.name: qsTr("Key hint opacity")
                            onMoved: AppSettings.setValue("keymap.hintOpacity", Math.round(value))
                        }
                    }
                }

                MenuSeparator {}

                Item {
                    implicitWidth: profileLabel.implicitWidth + 2 * (Theme.space.sm + Theme.space.xxs)
                    implicitHeight: Theme.size.pill + Theme.space.xs

                    Text {
                        id: profileLabel

                        x: Theme.space.sm + Theme.space.xxs
                        anchors.bottom: parent.bottom
                        textFormat: Text.PlainText
                        text: overlay.foregroundApp !== "" ? qsTr("Profile · %1").arg(overlay.foregroundApp)
                                                           : qsTr("Profile")
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                        font.weight: Font.DemiBold
                    }
                }

                Instantiator {
                    model: {
                        const usable = ProfileLibrary.entries.filter(entry => entry.error === "")
                        const app = overlay.foregroundApp
                        if (app === "")
                            return usable
                        return usable.filter(entry => entry.app === app)
                                     .concat(usable.filter(entry => entry.app !== app))
                    }

                    delegate: MenuItem {
                        required property var modelData

                        choice: true
                        chosen: modelData.id === overlay.profileId
                        text: modelData.name
                        shortcut: overlay.foregroundApp !== "" && modelData.app === overlay.foregroundApp
                                  ? qsTr("This game") : ""
                        onTriggered: DeviceManager.useProfile(overlay.serial, modelData.id)
                    }

                    onObjectAdded: (index, object) => keymapMenu.insertItem(keymapMenu.firstProfile + index, object)
                    onObjectRemoved: (index, object) => keymapMenu.removeItem(object)
                }

                MenuItem {
                    choice: true
                    chosen: overlay.profileId === "" && overlay.profileName === ""
                    text: qsTr("No profile")
                    onTriggered: DeviceManager.useProfile(overlay.serial, "")
                }

                MenuSeparator {}

                CommandMenuItem { commandState: editKeymapCommand }

                MenuItem {
                    objectName: "newProfile"
                    iconName: "plus"
                    text: qsTr("New profile…")
                    enabled: !overlay.editing && !overlay.mini
                    onTriggered: overlay.openStart()
                }

                MenuItem {
                    iconName: "settings-2"
                    text: qsTr("Manage profiles…")
                    onTriggered: profilesCommand.trigger()
                }
            }

            NewProfileDialog {
                id: newProfileDialog

                objectName: "newProfileDialog"
                serial: overlay.serial
                onCreated: internal.startOpened = false
            }
        }

        ToolbarSeparator {}

        CommandButton { id: backButton; commandState: backCommand; fallbackIcon: "arrow-left" }
        CommandButton { id: homeButton; commandState: homeCommand; fallbackIcon: "house" }
        CommandButton { id: appSwitchButton; commandState: appSwitchCommand; fallbackIcon: "square" }

        ToolbarSeparator { visible: !toolbar.tight }

        ToolbarButton {
            id: rotateButton

            objectName: "rotateButton"
            visible: !toolbar.tight
            iconName: "rotate-cw"
            text: overlay.noPicture ? qsTr("How the phone is held")
                                    : overlay.captureCard ? qsTr("Which way the phone turns") : qsTr("Orientation")
            enabled: overlay.noPicture || overlay.captureCard || followCommand.enabled || portraitCommand.enabled
                     || landscapeCommand.enabled
            onClicked: {
                if (overlay.noPicture)
                    turnMenu.open()
                else if (overlay.captureCard)
                    captureTurnMenu.open()
                else
                    rotateMenu.open()
            }

            Menu {
                id: rotateMenu

                x: (rotateButton.width - width) / 2
                y: rotateButton.height + Theme.space.xs

                CommandMenuItem { commandState: followCommand; choice: true }
                CommandMenuItem { commandState: portraitCommand; choice: true }
                CommandMenuItem { commandState: landscapeCommand; choice: true }
            }

            Menu {
                id: turnMenu

                objectName: "turnMenu"
                x: (rotateButton.width - width) / 2
                y: rotateButton.height + Theme.space.xs

                MenuLabel { text: qsTr("Without adb, tell how the phone is held") }
                TurnChoice { turn: 0; text: qsTr("Portrait") }
                TurnChoice { turn: 1; text: qsTr("Landscape (turned left)") }
                TurnChoice { turn: 2; text: qsTr("Landscape (turned right)") }
            }

            Menu {
                id: captureTurnMenu

                objectName: "captureTurnMenu"
                x: (rotateButton.width - width) / 2
                y: rotateButton.height + Theme.space.xs

                MenuLabel { text: qsTr("When the picture is landscape, the phone is") }
                TurnChoice { turn: 1; text: qsTr("Landscape (turned left)") }
                TurnChoice { turn: 2; text: qsTr("Landscape (turned right)") }
            }
        }

        CommandButton { id: screenButton; commandState: screenCommand; fallbackIcon: "power"; visible: !toolbar.compact }

        ToolbarButton {
            id: volumeButton

            visible: !toolbar.tight
            iconName: muteCommand.checked ? "volume-x" : "volume-2"
            text: qsTr("Volume")
            enabled: muteCommand.enabled
            selected: volumePanel.visible
            onClicked: volumePanel.open()

            T.Popup {
                id: volumePanel

                x: (volumeButton.width - width) / 2
                y: volumeButton.height + Theme.space.xs
                implicitWidth: implicitContentWidth + leftPadding + rightPadding
                implicitHeight: implicitContentHeight + topPadding + bottomPadding
                padding: Theme.space.xs
                rightPadding: Theme.space.md

                background: PopupSurface {
                    radius: height / 2
                }

                contentItem: Row {
                    spacing: Theme.space.xs

                    CommandButton {
                        anchors.verticalCenter: parent.verticalCenter
                        commandState: muteCommand
                        fallbackIcon: "volume-2"
                        checkedIcon: "volume-x"
                    }

                    Slider {
                        anchors.verticalCenter: parent.verticalCenter
                        implicitWidth: Theme.size.sliderCompact + valueWidth
                        enabled: !muteCommand.checked
                        from: 0
                        to: 1
                        value: overlay.volume
                        valueText: muteCommand.checked ? qsTr("Muted") : Math.round(value * 100) + "%"
                        onMoved: DeviceManager.setVolume(overlay.serial, value)
                    }
                }
            }
        }

        ToolbarButton {
            id: tasksButton

            objectName: "tasksButton"
            visible: !toolbar.tight
            iconName: "bot"
            text: overlay.taskRuns.length > 0 ? qsTr("Tasks: %n running", "", overlay.taskRuns.length) : qsTr("Tasks")
            selected: tasksMenu.visible || overlay.taskRuns.length > 0
            onClicked: tasksMenu.open()

            Menu {
                id: tasksMenu

                x: (tasksButton.width - width) / 2
                y: tasksButton.height + Theme.space.xs

                MenuLabel { text: qsTr("Tasks on this phone") }

                Instantiator {
                    model: TaskLibrary.tasks

                    delegate: MenuItem {
                        id: taskItem

                        required property var modelData
                        readonly property var run: overlay.taskRuns.find(each => each.task === taskItem.modelData.id)
                                                   ?? null

                        objectName: "taskItem"
                        iconName: taskItem.run !== null ? "square" : "play"
                        text: taskItem.run === null ? taskItem.modelData.name
                              : [taskItem.modelData.name, taskItem.run.progress, qsTr("running")]
                                .filter(part => part !== "").join(" · ")
                        onTriggered: {
                            if (taskItem.run !== null) {
                                DeviceManager.stopTask(taskItem.modelData.id, overlay.serial)
                                return
                            }
                            const problem = DeviceManager.startTask(taskItem.modelData.id, overlay.serial)
                            if (problem !== "")
                                overlay.toast(problem, "triangle-alert")
                        }
                    }

                    onObjectAdded: (index, object) => tasksMenu.insertItem(index + 1, object)
                    onObjectRemoved: (index, object) => tasksMenu.removeItem(object)
                }

                MenuItem {
                    visible: TaskLibrary.tasks.length === 0
                    height: visible ? implicitHeight : 0
                    enabled: false
                    text: qsTr("No tasks yet")
                }

                MenuSeparator {}

                MenuItem {
                    objectName: "openTasksPage"
                    iconName: "bot"
                    text: qsTr("Open the Tasks page")
                    onTriggered: tasksPageCommand.trigger()
                }
            }
        }

        ToolbarButton {
            id: recordButton

            objectName: "recordButton"
            visible: !toolbar.tight
            text: recordCommand.title
            shortcut: recordCommand.shortcut
            enabled: recordCommand.enabled
            selected: recordCommand.checked
            alert: true
            onClicked: recordCommand.trigger()

            Rectangle {
                anchors.centerIn: parent
                width: Theme.size.lockHalo
                height: width
                radius: width / 2
                color: recordCommand.checked ? Theme.colors.danger : recordButton.foreground
            }
        }

        ToolbarButton {
            id: extensionPanelsButton

            objectName: "extensionPanelsButton"
            visible: !toolbar.tight && overlay.sidePanels.length > 0
            iconName: "puzzle"
            text: qsTr("Extension panels")
            selected: extensionSide.shown || panelsMenu.visible
            onClicked: panelsMenu.open()

            Menu {
                id: panelsMenu

                objectName: "extensionPanelsMenu"
                x: (extensionPanelsButton.width - width) / 2
                y: extensionPanelsButton.height + Theme.space.xs

                MenuLabel { text: qsTr("Extension panels") }

                Instantiator {
                    model: overlay.sidePanels

                    delegate: MenuItem {
                        required property var modelData

                        objectName: "extensionPanelItem"
                        choice: true
                        chosen: extensionSide.shown && extensionSide.current === modelData.id
                        text: modelData.title
                        shortcut: modelData.extensionName
                        onTriggered: extensionSide.show(modelData.id)
                    }

                    onObjectAdded: (index, object) => panelsMenu.insertItem(index + 1, object)
                    onObjectRemoved: (index, object) => panelsMenu.removeItem(object)
                }

                MenuSeparator {}

                MenuItem {
                    objectName: "extensionPanelCloseItem"
                    iconName: "x"
                    text: qsTr("Close the panel")
                    enabled: extensionSide.shown
                    onTriggered: extensionSide.close()
                }
            }
        }

        Repeater {
            model: toolbar.tight ? 0 : overlay.toolbarButtons.length

            delegate: UiToolbarButton {
                id: extensionButton

                required property int index

                entry: overlay.toolbarButtons[extensionButton.index] ?? null
            }
        }

        ToolbarSeparator {}

        CommandButton { id: diagnosticsButton; commandState: diagnosticsCommand; fallbackIcon: "activity"; visible: !toolbar.compact }
        CommandButton {
            id: fullScreenButton

            visible: !toolbar.tight
            commandState: fullScreenCommand
            fallbackIcon: "maximize-2"
            checkedIcon: "minimize-2"
        }

        ToolbarButton {
            id: moreButton

            iconName: "ellipsis"
            text: qsTr("More")
            onClicked: moreMenu.open()

            UiToolbarPanel {
                id: toolbarPanel

                x: moreButton.width - width
                y: moreButton.height + Theme.space.xs
            }

            Menu {
                id: moreMenu

                x: moreButton.width - width
                y: moreButton.height + Theme.space.xs

                CommandMenuItem { commandState: screenCommand; visible: toolbar.compact && commandState.enabled }
                CommandMenuItem { commandState: diagnosticsCommand; visible: toolbar.compact && commandState.enabled }
                CommandMenuItem { commandState: fullScreenCommand; visible: toolbar.tight && commandState.enabled }
                CommandMenuItem { commandState: muteCommand; visible: toolbar.tight && commandState.enabled }
                MenuItem {
                    visible: toolbar.tight
                    height: visible ? implicitHeight : 0
                    iconName: "pin"
                    text: overlay.toolbarPinned ? qsTr("Unpin the toolbar") : qsTr("Pin the toolbar above the picture")
                    onTriggered: AppSettings.setValue("display.toolbar", overlay.toolbarPinned ? "dwell" : "pinned")
                }
                MenuLabel { text: qsTr("Orientation"); visible: toolbar.tight && rotateButton.enabled && !overlay.noPicture }
                CommandMenuItem { commandState: followCommand; choice: true; visible: toolbar.tight && commandState.enabled }
                CommandMenuItem {
                    commandState: portraitCommand
                    choice: true
                    visible: toolbar.tight && commandState.enabled
                }
                CommandMenuItem {
                    commandState: landscapeCommand
                    choice: true
                    visible: toolbar.tight && commandState.enabled
                }
                MenuLabel { text: qsTr("How the phone is held"); visible: toolbar.tight && overlay.noPicture }
                TurnChoice { turn: 0; text: qsTr("Portrait"); visible: toolbar.tight && overlay.noPicture }
                TurnChoice {
                    turn: 1
                    text: qsTr("Landscape (turned left)")
                    visible: toolbar.tight && overlay.noPicture
                }
                TurnChoice {
                    turn: 2
                    text: qsTr("Landscape (turned right)")
                    visible: toolbar.tight && overlay.noPicture
                }
                MenuLabel {
                    text: qsTr("When the picture is landscape, the phone is")
                    visible: toolbar.tight && overlay.captureCard
                }
                TurnChoice {
                    turn: 1
                    text: qsTr("Landscape (turned left)")
                    visible: toolbar.tight && overlay.captureCard
                }
                TurnChoice {
                    turn: 2
                    text: qsTr("Landscape (turned right)")
                    visible: toolbar.tight && overlay.captureCard
                }
                MenuLabel { text: qsTr("Status capsule") }
                CapsuleChoice { mode: "auto"; text: qsTr("Automatic") }
                CapsuleChoice { mode: "always"; text: qsTr("Always shown") }
                CapsuleChoice { mode: "hidden"; text: qsTr("Hidden") }
                MenuSeparator {}
                CommandMenuItem { objectName: "notificationsInMore"; commandState: notificationsCommand }
                CommandMenuItem { objectName: "quickSettingsInMore"; commandState: quickSettingsCommand }
                CommandMenuItem { objectName: "collapsePanelsInMore"; commandState: collapsePanelsCommand }
                MenuSeparator {
                    visible: notificationsCommand.enabled
                    height: visible ? implicitHeight : 0
                }
                MenuItem {
                    objectName: "phoneStatusItem"
                    choice: true
                    chosen: overlay.phoneStatusOn
                    enabled: overlay.phoneStatus.available === true
                    text: qsTr("Phone status")
                    onTriggered: DeviceManager.setPhoneStatus(overlay.serial, !overlay.phoneStatusOn)
                }
                MenuItem {
                    objectName: "tasksInMore"
                    visible: toolbar.tight
                    height: visible ? implicitHeight : 0
                    iconName: "bot"
                    text: qsTr("Tasks…")
                    onTriggered: tasksPageCommand.trigger()
                }
                CommandMenuItem {
                    objectName: "recordInMore"
                    commandState: recordCommand
                    visible: toolbar.tight && commandState.enabled
                }
                MenuItem {
                    objectName: "recordingsInMore"
                    visible: !overlay.noPicture
                    height: visible ? implicitHeight : 0
                    iconName: "history"
                    text: qsTr("Recordings…")
                    onTriggered: recordingsDialog.show(overlay.serial)
                }
                MenuItem {
                    id: panelsInMore

                    objectName: "extensionPanelsInMore"
                    visible: toolbar.tight && overlay.sidePanels.length > 0
                    height: visible ? implicitHeight : 0
                    iconName: "puzzle"
                    text: extensionSide.shown ? qsTr("Close the extension panel") : qsTr("Extension panel")
                    onTriggered: {
                        if (extensionSide.shown)
                            extensionSide.close()
                        else
                            extensionSide.reopen()
                    }
                }
                Instantiator {
                    model: toolbar.tight ? overlay.toolbarButtons.length : 0

                    delegate: MenuItem {
                        id: toolbarItem

                        required property int index
                        readonly property var buttonEntry: overlay.toolbarButtons[toolbarItem.index] ?? null
                        readonly property var look: toolbarItem.buttonEntry !== null
                                                    ? toolbarItem.buttonEntry.toolbar ?? ({}) : ({})

                        objectName: "extensionToolbarInMore"
                        text: (toolbarItem.look.title ?? "") !== "" ? toolbarItem.look.title
                              : toolbarItem.buttonEntry !== null ? toolbarItem.buttonEntry.title : ""
                        shortcut: toolbarItem.buttonEntry !== null ? toolbarItem.buttonEntry.extensionName : ""
                        iconName: toolbarItem.buttonEntry !== null && toolbarItem.buttonEntry.icon !== ""
                                  ? toolbarItem.buttonEntry.icon : "puzzle"
                        choice: toolbarItem.look.checked === true
                        chosen: true
                        enabled: toolbarItem.look.enabled !== false
                        onTriggered: {
                            const wanted = toolbarItem.buttonEntry
                            if (wanted === null)
                                return
                            ExtensionPanels.send(wanted.logic, wanted.name, "click", "", null)
                            toolbarPanel.openFor(wanted)
                        }
                    }

                    onObjectAdded: (index, object) => {
                        for (let at = 0; at < moreMenu.count; ++at) {
                            if (moreMenu.itemAt(at) === panelsInMore) {
                                moreMenu.insertItem(at + 1 + index, object)
                                return
                            }
                        }
                        moreMenu.addItem(object)
                    }
                    onObjectRemoved: (index, object) => moreMenu.removeItem(object)
                }
                MenuItem {
                    objectName: "editLogic"
                    iconName: "workflow"
                    text: qsTr("Edit logic")
                    onTriggered: {
                        const problem = DeviceManager.openLogicFor(overlay.serial)
                        if (problem !== "")
                            overlay.toast(problem, "triangle-alert")
                    }
                }
                MenuItem {
                    objectName: "visionWorkbench"
                    iconName: "target"
                    text: qsTr("Vision workbench")
                    enabled: overlay.profileId !== "" && !overlay.noPicture
                    shortcut: overlay.noPicture ? qsTr("No picture") : ""
                    onTriggered: {
                        const problem = DeviceManager.openVisionWorkbench(overlay.serial, overlay.profileId)
                        if (problem !== "")
                            overlay.toast(problem, "triangle-alert")
                    }
                }
                MenuItem {
                    objectName: "assistantInMore"
                    choice: true
                    chosen: AppSettings.values["assistant.mirrorBubble"] === true
                    text: qsTr("AI assistant bubble")
                    onTriggered: AppSettings.setValue("assistant.mirrorBubble",
                                                      AppSettings.values["assistant.mirrorBubble"] !== true)
                }
                CommandMenuItem {
                    objectName: "filesInMore"
                    commandState: filesCommand
                }
                MenuItem {
                    objectName: "adjustPictureArea"
                    visible: overlay.captureCard
                    height: visible ? implicitHeight : 0
                    iconName: "expand"
                    text: qsTr("Adjust the picture area…")
                    onTriggered: DeviceManager.beginCaptureCrop(overlay.serial)
                }
                MenuItem {
                    objectName: "automaticPictureArea"
                    visible: overlay.captureCard && overlay.manualCrop
                    height: visible ? implicitHeight : 0
                    iconName: "refresh-cw"
                    text: qsTr("Back to automatic")
                    onTriggered: DeviceManager.autoCaptureCrop(overlay.serial)
                }
                CommandMenuItem { commandState: latencyCommand }
                CommandMenuItem { commandState: miniCommand }
                MenuItem {
                    id: syncInMore

                    objectName: "syncPhones"
                    iconName: "link"
                    text: qsTr("Sync…")
                    enabled: overlay.syncRole !== "follower"
                    shortcut: overlay.syncRole === "follower" ? qsTr("Following %1").arg(overlay.syncLeader)
                              : overlay.syncRole === "master" ? qsTr("%n phone(s)", "", overlay.syncCount) : ""
                    onTriggered: syncDialog.show()
                }
                Instantiator {
                    model: MediaSinks.sinks

                    delegate: MenuItem {
                        id: sinkItem

                        required property var modelData
                        readonly property string sinkKey: sinkItem.modelData.key ?? ""

                        objectName: "mediaSinkInMore"
                        text: qsTr("Send to %1").arg(sinkItem.modelData.title ?? "")
                        shortcut: sinkItem.modelData.extensionName ?? ""
                        choice: true
                        chosen: MediaSinks.running.some(run => run.serial === overlay.serial
                                                               && run.key === sinkItem.sinkKey)
                        enabled: !overlay.noPicture || sinkItem.modelData.picture !== true
                        onTriggered: {
                            const problem = MediaSinks.toggle(overlay.serial, sinkItem.sinkKey)
                            if (problem !== "")
                                overlay.toast(problem, "triangle-alert")
                        }
                    }

                    onObjectAdded: (index, object) => {
                        for (let at = 0; at < moreMenu.count; ++at) {
                            if (moreMenu.itemAt(at) === syncInMore) {
                                moreMenu.insertItem(at + 1 + index, object)
                                return
                            }
                        }
                        moreMenu.addItem(object)
                    }
                    onObjectRemoved: (index, object) => moreMenu.removeItem(object)
                }
                CommandMenuItem { commandState: settingsCommand }
                CommandMenuItem { commandState: disconnectCommand }
            }
        }

        ToolbarSeparator { visible: !toolbar.tight }

        ToolbarButton {
            id: pinButton

            visible: !toolbar.tight
            iconName: "pin"
            text: overlay.toolbarPinned ? qsTr("Unpin the toolbar") : qsTr("Pin the toolbar above the picture")
            selected: overlay.toolbarPinned
            onClicked: AppSettings.setValue("display.toolbar", overlay.toolbarPinned ? "dwell" : "pinned")
        }
    }

    Item {
        anchors.fill: parent
        opacity: overlay.capsuleShown && !overlay.mini && !overlay.editing && !titleBar.statusRow ? 1 : 0
        visible: opacity > 0

        Behavior on opacity {
            id: capsuleFade

            NumberAnimation {
                duration: capsuleFade.targetValue > 0 ? Theme.motion.fast : Theme.motion.base
                easing.type: Easing.BezierSpline
                easing.bezierCurve: capsuleFade.targetValue > 0 ? Theme.motion.standard : Theme.motion.exit
            }
        }

        StatusCapsule {
            id: capsule

            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.space.lg
            anchors.rightMargin: Theme.space.lg + (extensionSide.shown ? extensionSide.width + Theme.space.sm : 0)
            framesPerSecond: overlay.framesPerSecond
            latencyMs: overlay.latencyMs
            transport: overlay.transport
            profileName: overlay.keymap ? overlay.profileName : ""
            task: overlay.taskText
            cameraLocked: overlay.keymap && overlay.cameraLocked
            problem: overlay.capsuleProblem
            recording: overlay.recordingOn
            recordingText: overlay.recordingText
            recordingNote: overlay.recordingNote

            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                acceptedButtons: Qt.LeftButton
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: diagnosticsCommand.trigger()
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: capsuleMenu.open()
            }

            CapsuleMenu {
                id: capsuleMenu

                x: capsule.width - width
                y: -height - Theme.space.xs
            }
        }

        UiStatusRow {
            objectName: "capsuleStatusWidgets"
            anchors.right: capsule.visible ? capsule.left : parent.right
            anchors.rightMargin: capsule.visible ? Theme.space.sm : capsule.anchors.rightMargin
            anchors.verticalCenter: capsule.verticalCenter
            visible: overlay.statusWidgets.length > 0
            widgets: overlay.statusWidgets
        }
    }

    StartPanel {
        id: startPanel

        objectName: "startPanel"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: overlay.capsuleMode !== "hidden" && !titleBar.statusRow
                              ? Theme.space.lg + Theme.size.capsule + Theme.space.sm : Theme.space.x3l
        width: Math.min(implicitWidth, overlay.width - 2 * Theme.space.lg)
        visible: overlay.startShown
        serial: overlay.serial
        game: overlay.startGame
        offered: overlay.startOffer !== "" && overlay.startGame === overlay.startOffer
        portrait: overlay.pictureRect.height > overlay.pictureRect.width
        onTemplateRequested: newProfileDialog.start(startPanel.game)
        onCommunityRequested: id => {
            DeviceManager.useProfile(overlay.serial, id)
            internal.startOpened = false
        }
        onImportRequested: startImport.open()
        onCreated: internal.startOpened = false
        onCloseRequested: overlay.closeStart()
        onFocusReleased: overlay.pictureFocusWanted()
    }

    FileDialog {
        id: startImport

        title: qsTr("Import a profile")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Key mapping profiles (*.json *.gsprofile)")]
        onAccepted: overlay.importForStart(startPanel.game, [startImport.selectedFile])
    }

    ScriptPrompt {
        id: scriptPrompt

        objectName: "scriptPrompt"
        anchors.horizontalCenter: parent.horizontalCenter
        y: toolbar.y + toolbar.height + Theme.space.sm
        onAnswered: (request, json) => {
            DeviceManager.answerPrompt(overlay.serial, Number(request), json)
            overlay.nextPrompt()
        }
    }

    LatencyTestCard {
        id: latencyCard

        objectName: "latencyCard"
        anchors.horizontalCenter: parent.horizontalCenter
        y: toolbar.y + toolbar.height + Theme.space.sm
        progress: overlay.latencyTest
        visible: shown && !overlay.mini && !overlay.editing
        onStopRequested: latencyCommand.trigger()
        onAgainRequested: latencyCommand.trigger()
        onDismissed: DeviceManager.dismissLatencyTest(overlay.serial)
    }

    KeymapEditor {
        id: keymapEditor

        objectName: "keymapEditor"
        anchors.fill: parent
        visible: overlay.editing
        serial: overlay.serial
        editor: overlay.editor
        picture: overlay.pictureRect
        noPicture: overlay.noPicture
        barTop: titleBar.titleHeight
        onNotify: (message, icon) => overlay.toast(message, icon)
    }

    LogicImportDialog {
        id: logicImport

        property string game

        objectName: "logicImportDialog"
        onConfirmed: urls => overlay.importStart(logicImport.game, urls, true)
    }

    RecordingDialog {
        id: recordingDialog

        objectName: "recordingDialog"
        onNotified: (message, icon) => overlay.toast(message, icon)
    }

    RecordingsDialog {
        id: recordingsDialog

        onChosen: (folder, serial) => {
            const info = Recordings.describe(folder, serial)
            if (info.folder === undefined)
                overlay.toast(qsTr("The recording is no longer there"), "triangle-alert")
            else
                recordingDialog.show(info)
        }
        onNotified: (message, icon) => overlay.toast(message, icon)
    }

    SyncDialog {
        id: syncDialog

        serial: overlay.serial
        syncing: overlay.syncRole === "master"
        onNotified: message => overlay.toast(message, "triangle-alert")
    }

    Dialog {
        id: closeEditing

        title: qsTr("Save the changes to this profile?")
        acceptText: qsTr("Save")
        rejectText: qsTr("Keep editing")
        alternativeText: qsTr("Don't save")

        Text {
            width: parent.width
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            text: qsTr("You are closing the window while %1 has changes that are not saved.")
                  .arg(overlay.editor !== null ? overlay.editor.profileName : "")
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.body.size
        }

        onAccepted: {
            DeviceManager.finishEditing(overlay.serial, true)
            if (!overlay.editing) {
                overlay.closeChosen = true
                overlay.closeWindow()
            }
        }
        onAlternativeChosen: {
            DeviceManager.finishEditing(overlay.serial, false)
            overlay.closeChosen = true
            overlay.closeWindow()
        }
    }

    Dialog {
        id: closeChoice

        objectName: "closeChoice"
        title: qsTr("Leave editing or close the window?")
        acceptText: qsTr("Leave editing")
        rejectText: qsTr("Cancel")
        alternativeText: qsTr("Close the mirror window")

        Column {
            width: parent.width
            spacing: Theme.space.md

            Text {
                width: parent.width
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: qsTr("Leaving editing saves your changes, like Done, and keeps the mirror window open.")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.body.size
            }

            Checkbox {
                id: rememberClose

                objectName: "rememberClose"
                text: qsTr("Remember my choice (you can change it in Settings > Display)")
            }
        }

        onAccepted: {
            if (rememberClose.checked)
                AppSettings.setValue("display.closeWhileEditing", "exitEditing")
            DeviceManager.finishEditing(overlay.serial, true)
        }
        onAlternativeChosen: {
            if (rememberClose.checked)
                AppSettings.setValue("display.closeWhileEditing", "closeWindow")
            overlay.closeAnyway()
        }
    }

    Item {
        objectName: "miniLayer"
        anchors.fill: parent
        visible: overlay.mini

        HoverHandler {
            id: miniHover
        }

        MouseArea {
            property point pressedAt
            property bool moving: false

            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onPressed: mouse => {
                pressedAt = Qt.point(mouse.x, mouse.y)
                moving = false
            }
            onPositionChanged: mouse => {
                if (!pressed || moving)
                    return
                if (Math.hypot(mouse.x - pressedAt.x, mouse.y - pressedAt.y) >= Application.styleHints.startDragDistance) {
                    moving = true
                    overlay.Window.window.startSystemMove()
                }
            }
            onDoubleClicked: miniCommand.trigger()
        }

        Row {
            objectName: "miniActions"
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.space.sm
            spacing: Theme.space.xs
            opacity: miniHover.hovered ? 1 : 0
            visible: opacity > 0

            Behavior on opacity {
                NumberAnimation {
                    duration: Theme.motion.fast
                }
            }

            Button {
                small: true
                tone: "secondary"
                iconName: "maximize-2"
                text: qsTr("Restore")
                onClicked: miniCommand.trigger()
            }

            Button {
                small: true
                tone: "secondary"
                iconName: "x"
                toolTip: qsTr("Close")
                Accessible.name: qsTr("Close")
                onClicked: overlay.Window.window.close()
            }
        }
    }

    DropArea {
        id: fileDrop

        property int apks: 0
        property int others: 0

        objectName: "fileDrop"
        anchors.fill: parent
        enabled: !overlay.editing && !overlay.mini
        onEntered: drag => {
            let apkCount = 0
            let otherCount = 0
            for (let i = 0; i < drag.urls.length; ++i) {
                const url = drag.urls[i].toString()
                if (!url.startsWith("file:"))
                    continue
                if (url.toLowerCase().endsWith(".apk"))
                    ++apkCount
                else
                    ++otherCount
            }
            fileDrop.apks = apkCount
            fileDrop.others = otherCount
            if (apkCount + otherCount === 0)
                drag.accepted = false
            else
                drag.accept(Qt.CopyAction)
        }
        onDropped: drop => {
            drop.accept(Qt.CopyAction)
            DeviceManager.dropOnMirror(overlay.serial, drop.urls)
        }

        Rectangle {
            anchors.fill: parent
            visible: fileDrop.containsDrag
            color: Theme.colors.bgFrosted
            border.width: Theme.size.focusRing
            border.color: Theme.colors.accentLine

            Column {
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.space.xl
                spacing: Theme.space.md

                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: fileDrop.others === 0 ? "smartphone" : "share"
                    size: Theme.size.iconHero
                    color: Theme.colors.accentText
                }

                Text {
                    objectName: "fileDropText"
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    text: fileDrop.others === 0 ? qsTr("Drop to install on the phone")
                          : fileDrop.apks === 0 ? qsTr("Drop to copy to the phone's Download folder")
                                                : qsTr("Drop to install the APKs and copy the other files to Download")
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.subtitle.size
                    font.weight: Theme.type.subtitle.weight
                }
            }
        }
    }

    Rectangle {
        objectName: "syncFrame"
        anchors.fill: parent
        visible: overlay.syncHue !== ""
        color: "transparent"
        border.width: overlay.syncRole === "master" ? Theme.size.syncFrame : Theme.size.syncFrameFollower
        border.color: Theme.toneInk(overlay.syncHue)
        opacity: overlay.syncRole === "master" ? 1 : Theme.fadedOpacity
    }

    Item {
        id: toastArea

        x: overlay.editing ? keymapEditor.view.x : 0
        y: overlay.editing ? keymapEditor.view.y : 0
        width: overlay.editing ? keymapEditor.view.width : overlay.width
        height: overlay.editing ? keymapEditor.view.height : overlay.height
    }

    Toast {
        id: notice

        parent: toastArea
        capsule: true
        y: overlay.editing ? Theme.space.sm
                           : toolbar.y + toolbar.height + Theme.space.sm
                             + (latencyCard.visible ? latencyCard.height + Theme.space.sm : 0)
        onActionTriggered: DeviceManager.toastAction(overlay.serial, 0)
        onSecondActionTriggered: DeviceManager.toastAction(overlay.serial, 1)
    }

    AiCorner {
        id: aiCorner

        objectName: "mirrorAiCorner"
        window: overlay.editing ? (keymapEditor.logicShown ? "logic" : "editor") : "mirror"
        allowed: !overlay.mini && (overlay.editing || AppSettings.values["assistant.mirrorBubble"] === true)
        editorUnsaved: keymapEditor.unsaved
        context: {
            const mirror = DeviceManager.mirrors.find(each => each.serial === overlay.serial) ?? {}
            const profile = overlay.editing ? (mirror.editingId ?? overlay.profileId) : overlay.profileId
            const entry = ProfileLibrary.entries.find(each => each.id === profile) ?? {}
            return {
                serial: overlay.serial,
                phone: mirror.name ?? "",
                app: overlay.foregroundApp,
                profile: profile,
                profileName: entry.name ?? overlay.profileName,
                selection: overlay.editing ? keymapEditor.selectionText : ""
            }
        }
    }

    Connections {
        target: DeviceManager

        function onAssistantWanted(serial: string) {
            if (serial === overlay.serial)
                Qt.callLater(aiCorner.show)
        }
    }

    Connections {
        target: MediaSinks

        function onStopped(serial: string, text: string) {
            if (serial === overlay.serial)
                overlay.toast(text, "triangle-alert")
        }
    }

    TourOverlay {
        id: tour

        anchors.fill: parent
        objectName: "mirrorTour"
        tourKey: "mirror"
        settings: AppSettings
        startWhen: overlay.Window.visibility !== Window.Hidden && overlay.Window.visibility !== Window.Minimized
                   && !overlay.mini && !overlay.editing && !overlay.startShown && toolbar.visible
                   && !AppSettings.toursQuiet
        version: Math.round(overlay.width) + Math.round(overlay.height)
        property int keymapFlips: 0
        onStarted: tour.keymapFlips = 0
        steps: [
            {
                centered: true,
                title: qsTr("The mirror window"),
                text: qsTr("This is the phone's screen. Clicking, dragging and scrolling here taps, swipes and "
                           + "scrolls on the phone, and what you type goes into the phone. The toolbar shows when "
                           + "the mouse moves to the top of the window.")
            },
            {
                target: keymapButton,
                title: qsTr("Key mapping"),
                text: qsTr("With key mapping on, the keyboard and mouse tap the screen the way the profile says, "
                           + "which is how you play games. Turn it off to type into the phone. Try it: click it "
                           + "twice."),
                keys: keymapCommand.shortcut
            },
            {
                target: keymapMenuButton,
                title: qsTr("Key mapping options"),
                text: qsTr("Switch profiles, show or hide the key hints, and open the editor where you place the "
                           + "keys on the picture.")
            },
            {
                target: homeButton,
                title: qsTr("Back, Home and Recent apps"),
                text: qsTr("The same as the three navigation keys at the bottom of the phone."),
                keys: [backCommand.shortcut, homeCommand.shortcut, appSwitchCommand.shortcut].filter(key => key !== "")
            },
            {
                target: rotateButton,
                optional: true,
                title: qsTr("Rotate"),
                text: qsTr("Turn the picture, or let the phone rotate by itself; the window changes shape with it.")
            },
            {
                target: screenButton,
                optional: true,
                title: qsTr("Turn the phone's screen off"),
                text: qsTr("The phone's own screen goes dark while you keep mirroring and working it here. It saves "
                           + "power and keeps the screen private."),
                keys: screenCommand.shortcut
            },
            {
                target: volumeButton,
                optional: true,
                title: qsTr("Volume"),
                text: qsTr("The phone's sound plays on this computer. Change how loud it is here or mute it; the "
                           + "phone's own volume stays as it is.")
            },
            {
                target: tasksButton,
                optional: true,
                title: qsTr("Tasks"),
                text: qsTr("Run a task on this phone, such as a script that plays for you, and see or stop the "
                           + "ones that are running.")
            },
            {
                target: recordButton,
                optional: true,
                title: qsTr("Record"),
                text: qsTr("Record the picture and sound to a video. A recording also lets you test logic later "
                           + "without the phone."),
                keys: recordCommand.shortcut
            },
            {
                target: extensionPanelsButton,
                optional: true,
                title: qsTr("Extension panels"),
                text: qsTr("Extensions that come with a panel open it beside the picture from here.")
            },
            {
                target: diagnosticsButton,
                optional: true,
                title: qsTr("Diagnostics"),
                text: qsTr("Shows the frame rate, the delay and how the picture is decoded. When things stutter, "
                           + "look here first."),
                keys: diagnosticsCommand.shortcut
            },
            {
                target: fullScreenButton,
                optional: true,
                title: qsTr("Full screen"),
                text: qsTr("The picture fills the whole screen; do it again to come back."),
                keys: fullScreenCommand.shortcut !== "" ? fullScreenCommand.shortcut : "F11"
            },
            {
                target: moreButton,
                title: qsTr("More"),
                text: qsTr("The notification shade, quick settings, recordings, editing logic, the vision "
                           + "workbench, files, the small window, syncing phones and this phone's settings are in "
                           + "here. When the window is narrow, the buttons that do not fit are in here too.")
            },
            {
                target: pinButton,
                optional: true,
                title: qsTr("Pin the toolbar"),
                text: qsTr("Pinned, the toolbar stays above the picture instead of hiding until the mouse comes up.")
            },
            {
                centered: true,
                title: qsTr("Ready to play"),
                text: qsTr("To have keys tap the buttons of a game, open Edit key mapping under Key mapping options "
                           + "and drag keys onto the picture. Help can replay this tour.")
            }
        ]
        advance: {
            switch (tour.step) {
            case 1:
                return tour.keymapFlips >= 2
            }
            return false
        }

        Connections {
            target: overlay

            function onKeymapChanged() {
                if (tour.running && tour.step === 1)
                    ++tour.keymapFlips
            }
        }
    }
}
