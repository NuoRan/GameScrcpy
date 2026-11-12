pragma ComponentBehavior: Bound

import QtQuick
import QtQml.Models
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App
import "ExtensionLook.js" as ExtensionLook

Item {
    id: page

    property string current: "video"
    property var phone: ({})
    readonly property real titleReserve: scopeRow.visible ? scopeRow.width + Theme.space.lg : 0
    readonly property int nativeSide: phone.longSide ?? 0
    readonly property bool esp32Installed: ExtensionHost.entries.some(entry => entry.id === "gamescrcpy.esp32")
    readonly property var esp32Entry: ExtensionHost.entries.find(entry => entry.id === "gamescrcpy.esp32"
                                                                 && entry.superseded !== true) ?? null
    readonly property var ownGrants: HardwareAccess.grants["mine"] ?? []
    readonly property var ownFolders: FileAccess.folders["mine"] ?? []
    readonly property var settingsPanels: ExtensionPanels.panels.filter(entry => entry.where === "settings")
    readonly property bool extensionsShown: SettingsCatalog.extensionGroups.length > 0 || page.settingsPanels.length > 0

    readonly property var groups: [
        { name: "video", text: qsTr("Mirroring & quality"), icon: "monitor" },
        { name: "audio", text: qsTr("Audio"), icon: "volume-2" },
        { label: qsTr("Control") },
        { name: "input", text: qsTr("Input & key mapping"), icon: "gamepad-2" },
        { name: "shortcuts", text: qsTr("Keyboard shortcuts"), icon: "keyboard" },
        { name: "connection", text: qsTr("Connection"), icon: "usb" },
        { label: qsTr("System") },
        { name: "general", text: qsTr("General"), icon: "sliders-horizontal" },
        { name: "appearance", text: qsTr("Appearance"), icon: "palette" },
        { name: "extensions", text: qsTr("Extensions"), icon: "puzzle" },
        { name: "ai", text: qsTr("AI"), icon: "sparkles" },
        { name: "about", text: qsTr("About"), icon: "info" }
    ].filter(group => group.name !== "extensions" || page.extensionsShown)
     .filter(group => group.name !== "connection" || page.esp32Installed)

    signal diagnosticsRequested()

    onExtensionsShownChanged: {
        if (!page.extensionsShown && page.current === "extensions")
            page.current = "video"
    }

    function open(name: string) {
        search.clear()
        current = name === "assistant" ? "ai" : name
        content.contentY = 0
    }

    function pagesOf(name: string): var {
        return name === "ai" ? ["ai", "assistant"] : [name]
    }

    function focusSearch() {
        search.forceActiveFocus(Qt.ShortcutFocusReason)
    }

    function startTour() {
        tour.restart()
    }

    TourOverlay {
        id: tour

        anchors.fill: parent
        objectName: "settingsTour"
        tourKey: "settings"
        settings: AppSettings
        startWhen: page.visible && !AppSettings.toursQuiet
        steps: [
            {
                centered: true,
                title: qsTr("Settings"),
                text: qsTr("Every setting is here, and a change takes effect right away; there is nothing to save.")
            },
            {
                target: search,
                title: qsTr("Search settings"),
                text: qsTr("Not sure where a setting is? Search for it, such as frame rate or key hints."),
                keys: searchCommand.shortcut
            },
            {
                target: nav,
                title: qsTr("Groups"),
                text: qsTr("The settings are grouped: mirroring and quality, audio, input and keys, shortcuts, "
                           + "connection, general, appearance, extensions and AI. A number next to a group counts "
                           + "the settings a phone has changed for itself.")
            },
            {
                target: content,
                title: qsTr("The settings"),
                text: qsTr("Each row says what it is for on the left and has its control on the right.")
            },
            {
                target: scopeRow,
                optional: true,
                title: qsTr("All phones or this phone"),
                text: qsTr("With a phone connected, choose this phone to change a setting for it alone; the rest "
                           + "follow the default for all phones. Reset takes a changed setting back to that "
                           + "default.")
            },
            {
                centered: true,
                title: qsTr("That's settings"),
                text: qsTr("The AI assistant's service and key are set up under AI. Help can replay this tour.")
            }
        ]
    }

    function openDevicePage() {
        const pages = SettingsCatalog.perDevicePages
        const first = page.groups.find(group => pages.indexOf(group.name) >= 0)
        page.open(pages.indexOf(page.current) >= 0 || first === undefined ? page.current : first.name)
    }

    function shows(name: string, searchable: bool): bool {
        if (!SettingsCatalog.searching)
            return current === name
        return searchable && page.pagesOf(name).some(part => (SettingsCatalog.pageHits[part] ?? 0) > 0)
    }

    function reveal(item: Item) {
        let shown = item
        let ancestor = item
        while (ancestor !== null && ancestor !== content.contentItem) {
            if ((ancestor as SettingRow) !== null)
                shown = ancestor
            ancestor = ancestor.parent
        }
        if (ancestor === null)
            return
        const top = shown.mapToItem(content.contentItem, 0, 0).y - Theme.space.lg
        const bottom = top + shown.height + 2 * Theme.space.lg
        if (top < content.contentY)
            content.contentY = Math.max(0, top)
        else if (bottom > content.contentY + content.height)
            content.contentY = Math.min(bottom - content.height, content.contentHeight - content.height)
    }

    function refreshPhone() {
        phone = SettingsCatalog.device === "" ? ({}) : DeviceManager.phoneDetails(SettingsCatalog.device)
    }

    function aligned(length: real, limit: int): int {
        let result = Math.round(length / 8) * 8
        if (limit > 0 && result > limit)
            result = Math.floor(limit / 8) * 8
        return Math.max(8, result)
    }

    function sentSize(maxSize: int): string {
        const longSide = phone.longSide ?? 0
        const shortSide = phone.shortSide ?? 0
        if (longSide === 0)
            return ""
        const scale = maxSize > 0 && longSide > maxSize ? maxSize / longSide : 1
        const own = longSide + " × " + shortSide
        const sent = aligned(longSide * scale, maxSize) + " × " + aligned(shortSide * scale, maxSize)
        return sent === own ? own : own + " → " + sent
    }

    function refreshText(): string {
        const hertz = phone.refreshHz ?? 0
        return hertz > 0 ? qsTr("The phone is at %1 Hz now").arg(Math.round(hertz)) : ""
    }

    function toggleLabels(reducedBySystem: bool): var {
        return {
            system: qsTr("Follow Windows (%1)").arg(reducedBySystem ? qsTr("on") : qsTr("off")),
            on: qsTr("On"),
            off: qsTr("Off")
        }
    }

    Component.onCompleted: {
        refreshPhone()
        if (SettingsCatalog.scoped)
            openDevicePage()
    }

    CommandState {
        id: searchCommand

        command: "settings.search"
    }

    CommandState {
        id: hintsCommand

        command: "device.keyHints"
    }

    Connections {
        target: page.Window.window

        function onActiveFocusItemChanged() {
            const item = page.Window.activeFocusItem
            const reason = (item as T.Control)?.focusReason ?? (item as T.TextField)?.focusReason
            if (reason === Qt.TabFocusReason || reason === Qt.BacktabFocusReason || reason === Qt.ShortcutFocusReason)
                page.reveal(item)
        }
    }

    Connections {
        target: SettingsCatalog

        function onQueryChanged() {
            content.contentY = 0
        }

        function onEntriesChanged() {
            if (page.current === "extensions" && !page.extensionsShown)
                page.current = "video"
            if (page.current === "connection" && !ExtensionHost.entries.some(entry => entry.id === "gamescrcpy.esp32"))
                page.current = "video"
        }

        function onScopeChanged() {
            page.refreshPhone()
        }

        function onOpenRequested() {
            page.openDevicePage()
        }
    }

    Connections {
        target: DeviceManager

        function onDetailsChanged() {
            page.refreshPhone()
        }
    }

    TextMetrics {
        id: deviceName

        font: scopeChoice.font
        text: SettingsCatalog.scopeDeviceName
        elide: Text.ElideRight
        elideWidth: Theme.size.scopeName
    }

    Item {
        id: nav

        width: Theme.size.sideNav
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        SearchField {
            id: search

            x: Theme.space.xl
            y: Theme.space.xxl
            width: parent.width - Theme.space.xl - Theme.space.md
            placeholderText: qsTr("Search settings")
            shortcut: searchCommand.shortcut
            Keys.onEscapePressed: clear()
            onTextChanged: SettingsCatalog.query = text
        }

        Column {
            anchors.top: search.bottom
            anchors.topMargin: Theme.space.md + Theme.space.xxs
            x: search.x
            width: search.width
            spacing: Theme.space.xxs

            Repeater {
                model: page.groups

                delegate: Item {
                    id: entry

                    required property var modelData
                    readonly property bool heading: modelData.label !== undefined

                    width: parent ? parent.width : implicitWidth
                    height: heading ? groupLabel.implicitHeight : groupButton.implicitHeight

                    Text {
                        id: groupLabel

                        visible: entry.heading
                        leftPadding: Theme.space.sm + Theme.space.xxs
                        topPadding: Theme.space.md + Theme.space.xxs
                        bottomPadding: Theme.space.xs
                        textFormat: Text.PlainText
                        text: entry.modelData.label ?? ""
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.micro.size
                        font.weight: Theme.type.micro.weight
                    }

                    GroupItem {
                        id: groupButton

                        visible: !entry.heading
                        width: parent.width
                        text: entry.modelData.text ?? ""
                        iconName: entry.modelData.icon ?? ""
                        count: (SettingsCatalog.pageOverrides[entry.modelData.name] ?? []).length
                        current: !SettingsCatalog.searching && page.current === entry.modelData.name
                        onClicked: page.open(entry.modelData.name)
                    }
                }
            }
        }

        Rectangle {
            anchors.right: parent.right
            width: Theme.size.stroke
            height: parent.height
            color: Theme.colors.strokeSubtle
        }
    }

    Flickable {
        id: content

        anchors.left: nav.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        contentWidth: width
        contentHeight: sections.y + sections.implicitHeight + Theme.size.panelPaddingV
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Row {
            id: scopeRow

            anchors.right: sections.right
            y: sections.y + (Theme.type.title.lineHeight - height) / 2
            z: 1
            visible: SettingsCatalog.scopeDevice !== ""
            spacing: Theme.space.sm + Theme.space.xxs

            Text {
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: qsTr("Applies to")
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
            }

            SegmentedControl {
                id: scopeChoice

                model: [qsTr("Global default"), { text: deviceName.elidedText, icon: "smartphone" }]
                currentIndex: SettingsCatalog.scoped ? 1 : 0
                Accessible.name: qsTr("Applies to")
                onActivated: index => {
                    SettingsCatalog.scoped = index === 1
                    scopeChoice.currentIndex = Qt.binding(() => SettingsCatalog.scoped ? 1 : 0)
                }
            }
        }

        Column {
            id: sections

            x: Theme.size.panelPaddingH
            y: Theme.space.xxl
            width: content.width - 2 * Theme.size.panelPaddingH
            spacing: Theme.space.x3l

            Section {
                name: "video"
                title: qsTr("Mirroring & quality")
                titleReserve: page.titleReserve
                shown: page.shows(name, true)

                SettingCard {
                    title: qsTr("Video")
                    iconName: "monitor"
                    tone: "orange"
                    note: qsTr("Changes take effect from the next connection.")

                    SliderSetting {
                        path: "video.maxSize"
                        unit: "px"
                        step: 40
                        sliderFrom: 480
                        sliderTo: page.nativeSide > 0 ? page.nativeSide : 3200
                        marks: page.nativeSide > 0
                               ? [720, 1080, 1440, 1920, 2400, 3200].filter(mark => mark < page.nativeSide - 160)
                                     .concat([{ value: page.nativeSide, text: qsTr("Original %1").arg(page.nativeSide) }])
                               : [720, 1080, 1440, 1920, 2400, 3200]
                        info: page.sentSize(value ?? 0)
                        special: ({
                            value: 0,
                            text: qsTr("Original resolution"),
                            word: qsTr("Original"),
                            fallback: 1920,
                            note: page.nativeSide > 0 ? page.sentSize(0) : qsTr("Sends the phone's own resolution")
                        })
                    }
                    SliderSetting {
                        path: "video.maxFps"
                        unit: "fps"
                        sliderFrom: 1
                        marks: [30, 60, 90, 120, 144, 240]
                        info: page.refreshText()
                        special: ({
                            value: 0,
                            text: qsTr("Follow phone"),
                            fallback: 60,
                            note: page.refreshText() !== "" ? page.refreshText()
                                                             : qsTr("Each phone uses its own refresh rate")
                        })
                    }
                    SelectSetting {
                        path: "video.codec"
                        labels: ({ h264: qsTr("H.264 (AVC)"), h265: qsTr("H.265 (HEVC)"), av1: "AV1" })
                        fact: (page.phone.codecs ?? []).length > 0
                              ? qsTr("This phone supports %1").arg(page.phone.codecs.join(" · ")) : ""
                    }
                    SliderSetting {
                        path: "video.bitrateKbps"
                        unit: "Mb/s"
                        factor: 1000
                        decimals: 1
                        logarithmic: true
                        sliderFrom: 1
                        sliderTo: 100
                        marks: [1, 2, 4, 8, 16, 32, 64, 100]
                    }
                }

                SettingCard {
                    title: qsTr("Display")
                    iconName: "gauge"
                    tone: "azure"

                    SwitchSetting {
                        path: "display.lowLatency"
                    }
                    SliderSetting {
                        path: "display.sharpness"
                        unit: "%"
                        step: 5
                    }
                    SelectSetting {
                        path: "display.titleBar"
                        labels: ({ fixed: qsTr("Fixed above the picture"), auto: qsTr("Shown with the toolbar") })
                    }
                    SelectSetting {
                        path: "display.toolbar"
                        labels: ({ dwell: qsTr("Rest the pointer at the top"), pinned: qsTr("Pinned above the picture"),
                                   shortcut: qsTr("Shortcut only") })
                    }
                    SelectSetting {
                        path: "display.statusCapsule"
                        labels: ({ auto: qsTr("Automatic"), always: qsTr("Always shown"), hidden: qsTr("Hidden") })
                    }
                    SwitchSetting {
                        objectName: "phoneStatusSwitch"
                        path: "display.phoneStatus"
                    }
                    SwitchSetting {
                        objectName: "cameraGlowSwitch"
                        path: "display.cameraGlow"
                    }
                    SelectSetting {
                        objectName: "closeWhileEditing"
                        path: "display.closeWhileEditing"
                        labels: ({ ask: qsTr("Ask each time"), exitEditing: qsTr("Leave editing"),
                                   closeWindow: qsTr("Close the mirror window") })
                    }
                }

                SettingCard {
                    title: qsTr("Recording")
                    iconName: "camera"
                    tone: "rose"

                    FolderSetting {
                        objectName: "recordingLocation"
                        path: "capture.folder"
                    }
                }
            }

            Section {
                name: "audio"
                title: qsTr("Audio")
                titleReserve: page.titleReserve
                shown: page.shows(name, true)

                SettingCard {
                    title: qsTr("Sound")
                    iconName: "volume-2"
                    tone: "teal"
                    note: qsTr("Changes take effect from the next connection.")

                    SwitchSetting {
                        id: sound

                        path: "audio.enabled"
                    }
                    SliderSetting {
                        path: "audio.bitrateKbps"
                        unit: "kb/s"
                        logarithmic: true
                        marks: [32, 64, 128, 256, 512]
                        enabled: sound.value === true
                    }
                }

                SpeechSettings {
                    objectName: "speechSettings"
                }
            }

            Section {
                name: "input"
                title: qsTr("Input & key mapping")
                info: qsTr("What the camera lock key and each mouse button do belongs to the key components of each "
                           + "profile; set them in the key editor.")
                titleReserve: page.titleReserve
                shown: page.shows(name, true)

                SettingCard {
                    title: qsTr("Key hints")
                    iconName: "keyboard"
                    tone: "teal"

                    SwitchSetting {
                        id: hints

                        path: "keymap.hints"
                        keys: hintsCommand.shortcut === "" ? [] : Commands.shortcutCaps(hintsCommand.command)
                    }
                    SliderSetting {
                        path: "keymap.hintOpacity"
                        unit: "%"
                        step: 5
                        enabled: hints.value === true
                    }
                    SwitchSetting {
                        id: intro

                        path: "keymap.hintIntro"
                        enabled: hints.value === true
                    }
                    SliderSetting {
                        path: "keymap.hintIntroMs"
                        unit: qsTr("s")
                        factor: 1000
                        step: 0.5
                        decimals: 1
                        nested: true
                        enabled: hints.value === true && intro.value === true
                    }
                }

                SettingCard {
                    title: qsTr("Profiles")
                    iconName: "layers"
                    tone: "amber"

                    SwitchSetting {
                        path: "keymap.autoProfile"
                    }
                }

                SettingCard {
                    objectName: "phoneCursorCard"
                    title: qsTr("GameScrcpy Companion on the phone")
                    iconName: "monitor-smartphone"
                    tone: "lime"

                    SwitchSetting {
                        objectName: "phoneImeSwitch"
                        path: "companion.ime"
                    }
                    SwitchSetting {
                        objectName: "phoneHintsSwitch"
                        path: "companion.hints"
                    }
                    SliderSetting {
                        objectName: "phoneHintOpacity"
                        path: "companion.hintOpacity"
                        unit: "%"
                        step: 5
                        nested: true
                    }
                    SwitchSetting {
                        objectName: "phoneCursorSwitch"
                        path: "companion.cursor"
                    }
                    SwitchSetting {
                        objectName: "phoneAutoConnectSwitch"
                        path: "companion.autoConnect"
                    }
                }

                SettingCard {
                    objectName: "pictureCard"
                    title: qsTr("Picture")
                    iconName: "image"
                    tone: "orange"

                    PictureSourceSetting {
                        id: pictureSource

                        objectName: "pictureSource"
                        path: "picture.source"
                        labels: ({ phone: qsTr("Phone"), capture: qsTr("Capture card"), none: qsTr("No picture") })
                    }

                    Item {
                        objectName: "pictureCaptureLink"
                        visible: pictureSource.captureOpenable && pictureSource.visible
                        width: parent.width
                        height: visible ? captureSettingsButton.implicitHeight + Theme.space.md : 0

                        Button {
                            id: captureSettingsButton

                            objectName: "pictureCaptureSettings"
                            x: pictureSource.inset - Theme.space.sm
                            tone: "ghost"
                            small: true
                            iconName: "monitor-play"
                            text: qsTr("Capture card settings…")
                            onClicked: Commands.trigger("device.settings", pictureSource.captureSerial)
                        }
                    }
                }

                SettingCard {
                    title: qsTr("Touch")
                    iconName: "smartphone"
                    tone: "azure"

                    TouchMethodSetting {
                        id: touchMethod

                        property string pending
                        readonly property string explanation: {
                            if (touchMethod.methodMissing)
                                return qsTr("The extension that adds this touch method is off, so touches go the "
                                            + "normal way.")
                            if (touchMethod.method !== null)
                                return touchMethod.method.description !== ""
                                       ? qsTr("%1 From %2; when it fails, touches go the normal way.")
                                         .arg(touchMethod.method.description).arg(touchMethod.method.extension)
                                       : qsTr("From %1; when it fails, touches go the normal way.")
                                         .arg(touchMethod.method.extension)
                            if (touchMethod.current === "uhid")
                                return qsTr("Up to 16 fingers. On an independent display touches still go the normal way.")
                            if (touchMethod.current === "aoa")
                                return qsTr("A touch screen, mouse and keyboard on the USB cable that work even without "
                                            + "USB debugging. On Windows USB debugging cannot be used while AOA is, so "
                                            + "the picture comes over wireless debugging when the phone has it, "
                                            + "otherwise there is no picture. Needs the WinUSB driver.")
                            if (touchMethod.current === "none")
                                return qsTr("Only shows the picture; taps, swipes and key mapping do not reach the phone.")
                            return ""
                        }

                        objectName: "touchMethod"
                        path: "touch.method"
                        labels: ({ inject: qsTr("Normal"), uhid: qsTr("UHID touch screen"), aoa: qsTr("AOA"),
                                   none: qsTr("No touch") })
                        confirmFirst: ["aoa"]
                        fact: [touchMethod.explanation, touchMethod.blockedText].filter(text => text !== "").join("\n")
                        onConfirmationWanted: option => touchMethod.pending = String(option)
                        onCurrentChanged: touchMethod.pending = ""
                    }

                    SettingRow {
                        id: touchDevice

                        readonly property string chosen: String(touchDevice.value ?? "")
                        readonly property var choices: {
                            const ids = [""].concat(HardwareAccess.devices.map(entry => entry.id))
                            return ids.indexOf(touchDevice.chosen) >= 0 ? ids : ids.concat([touchDevice.chosen])
                        }

                        function titleOf(id: string): string {
                            return id === "" ? qsTr("None") : HardwareAccess.deviceTitle(id)
                        }

                        objectName: "touchDevice"
                        path: "touch.device"
                        nested: true
                        visible: touchDevice.matched && touchMethod.method !== null
                                 && touchMethod.method.wantsDevice === true
                        globalText: touchDevice.titleOf(String(touchDevice.globalValue ?? ""))

                        Select {
                            id: deviceSelect

                            objectName: "touchDeviceSelect"
                            model: touchDevice.choices.map(id => touchDevice.titleOf(id))
                            currentIndex: touchDevice.choices.indexOf(touchDevice.chosen)
                            Accessible.name: touchDevice.title
                            onActivated: index => {
                                if (!touchDevice.commit(touchDevice.choices[index]))
                                    deviceSelect.currentIndex = Qt.binding(() => touchDevice.choices.indexOf(touchDevice.chosen))
                            }
                        }
                    }

                    Item {
                        objectName: "aoaConfirm"
                        visible: touchMethod.pending !== "" && touchMethod.visible
                        width: parent.width
                        height: visible ? aoaColumn.implicitHeight + Theme.space.md : 0

                        Column {
                            id: aoaColumn

                            x: touchMethod.inset
                            width: parent.width - 2 * touchMethod.inset
                            spacing: Theme.space.sm

                            InlineAlert {
                                width: parent.width
                                tone: "warn"
                                title: qsTr("Without wireless debugging, AOA has no picture")
                                detail: qsTr("You then watch the phone itself while the keys work. These do not work "
                                             + "without a picture: %1")
                                        .arg(DeviceManager.noPictureLimits().join(qsTr(", ")))
                                actionText: qsTr("Use AOA")
                                onActionTriggered: {
                                    const option = touchMethod.pending
                                    touchMethod.pending = ""
                                    touchMethod.choose(option)
                                }
                            }

                            Button {
                                objectName: "aoaCancel"
                                tone: "ghost"
                                small: true
                                text: qsTr("Keep the current touch method")
                                onClicked: touchMethod.pending = ""
                            }
                        }
                    }

                    Item {
                        objectName: "aoaGuide"
                        visible: (touchMethod.current === "aoa" || touchMethod.pending !== "") && touchMethod.visible
                        width: parent.width
                        height: visible ? guideButton.implicitHeight + Theme.space.md : 0

                        Button {
                            id: guideButton

                            x: touchMethod.inset - Theme.space.sm
                            tone: "ghost"
                            small: true
                            iconName: "circle-help"
                            text: qsTr("How to install the WinUSB driver")
                            onClicked: Commands.trigger("app.help", "aoa-driver")
                        }
                    }
                }
            }

            Section {
                name: "shortcuts"
                title: qsTr("Keyboard shortcuts")
                info: qsTr("The same commands as in the command palette. When the current profile binds a key too, "
                           + "the game gets it first.")
                titleReserve: page.titleReserve
                shown: page.shows(name, true)
                tools: [
                    Button {
                        visible: Commands.customized && !SettingsCatalog.searching
                        tone: "ghost"
                        small: true
                        iconName: "rotate-ccw"
                        text: qsTr("Restore all defaults")
                        onClicked: Commands.resetAllShortcuts()
                    }
                ]

                ShortcutCard {
                    title: qsTr("Mirror window")
                    iconName: "monitor-play"
                    tone: "orange"
                    device: true
                }

                ShortcutCard {
                    title: qsTr("Any window")
                    iconName: "panel-top"
                    tone: ""
                    device: false
                }
            }

            Section {
                name: "connection"
                title: qsTr("Connection")
                titleReserve: page.titleReserve
                shown: page.esp32Installed && page.shows(name, true)

                Card {
                    objectName: "esp32SettingsCard"
                    width: parent ? parent.width : implicitWidth
                    padding: 0
                    horizontalPadding: 0

                    SettingsRow {
                        objectName: "esp32SettingsRow"
                        width: parent ? parent.width : implicitWidth
                        title: qsTr("ESP32 touch box")
                        highlight: SettingsCatalog.searching ? SettingsCatalog.query : ""
                        note: {
                            if (SettingsCatalog.searching)
                                return ["esp32.link", "esp32.port", "esp32.address"]
                                    .filter(path => SettingsCatalog.hits[path] === true)
                                    .map(path => SettingsCatalog.entries[path]?.title ?? path)
                                    .join(" · ")
                            if (page.esp32Entry === null || page.esp32Entry.enabled !== true)
                                return qsTr("The extension is off. Turn it on in Extensions to use the board.")
                            return page.esp32Entry.active === true ? "" : qsTr("The extension is not loaded.")
                        }

                        Button {
                            objectName: "esp32OpenSettings"
                            small: true
                            iconName: "sliders-horizontal"
                            enabled: page.esp32Entry !== null && page.esp32Entry.active === true
                            text: qsTr("Open settings")
                            onClicked: Commands.trigger("esp32.settings", "")
                        }
                    }
                }
            }

            Section {
                name: "general"
                title: qsTr("General")
                titleReserve: page.titleReserve
                shown: page.shows(name, true)

                SettingCard {
                    objectName: "trayCard"
                    title: qsTr("Scheduled tasks")
                    iconName: "timer"
                    tone: "teal"

                    SwitchSetting {
                        objectName: "closeToTraySwitch"
                        path: "tray.closeToTray"
                    }
                }

                SettingCard {
                    objectName: "ownDevices"
                    title: qsTr("Devices your own logic may use")
                    iconName: "usb"
                    tone: "amber"
                    visible: !SettingsCatalog.searching && SettingsCatalog.device === "" && page.ownGrants.length > 0

                    AllowedDevices {
                        owner: "mine"
                    }
                }

                SettingCard {
                    objectName: "linksCard"
                    title: qsTr("Extension links")
                    iconName: "link"
                    tone: "azure"
                    visible: ExtensionLinks.portable && !SettingsCatalog.searching && SettingsCatalog.device === ""

                    SettingsRow {
                        objectName: "linksRow"
                        width: parent ? parent.width : implicitWidth
                        title: qsTr("Open gamescrcpy:// links with this copy")
                        note: ExtensionLinks.registration === "this"
                              ? qsTr("Links on web pages that install or show extensions open in this copy.")
                              : ExtensionLinks.registration === "other"
                                ? qsTr("Another copy of GameScrcpy opens these links now.")
                                : qsTr("Web pages can offer extensions through gamescrcpy:// links. This portable copy "
                                       + "opens them once you register it.")

                        Button {
                            objectName: "registerLinks"
                            small: true
                            iconName: "link"
                            text: ExtensionLinks.registration === "this" ? qsTr("Remove gamescrcpy:// links")
                                                                         : qsTr("Register gamescrcpy:// links")
                            onClicked: {
                                if (ExtensionLinks.registration === "this")
                                    ExtensionLinks.unregisterLinks()
                                else
                                    ExtensionLinks.registerLinks()
                            }
                        }
                    }
                }

                SettingCard {
                    objectName: "ownFolders"
                    title: qsTr("Folders your own logic may use")
                    iconName: "folder-open"
                    tone: "amber"
                    visible: !SettingsCatalog.searching && SettingsCatalog.device === "" && page.ownFolders.length > 0

                    AllowedFolders {
                        owner: "mine"
                    }
                }
            }

            Section {
                name: "appearance"
                title: qsTr("Appearance")
                titleReserve: page.titleReserve
                shown: page.shows(name, true)

                SettingCard {
                    title: qsTr("Theme and color")
                    iconName: "palette"
                    tone: "rose"

                    ThemeSetting {
                        path: "appearance.theme"
                        labels: ({
                            dark: qsTr("Dark"),
                            light: qsTr("Light"),
                            system: Application.styleHints.colorScheme === Qt.ColorScheme.Light
                                    ? qsTr("Follow Windows (light)") : qsTr("Follow Windows (dark)")
                        })
                    }
                    AccentSetting {
                        path: "appearance.accent"
                        labels: ({
                            ember: qsTr("Ember"),
                            teal: qsTr("Teal"),
                            azure: qsTr("Azure"),
                            lime: qsTr("Lime"),
                            rose: qsTr("Rose"),
                            amber: qsTr("Amber")
                        })
                    }
                    SettingsRow {
                        width: parent ? parent.width : implicitWidth
                        visible: !SettingsCatalog.searching && SettingsCatalog.device === "" && paletteSelect.themes.length > 0
                        divider: true
                        title: qsTr("Color theme")
                        description: qsTr("Colors from installed extensions")

                        Select {
                            id: paletteSelect

                            readonly property var themes: ExtensionHost.themes

                            model: [qsTr("Default")].concat(paletteSelect.themes.map(theme => theme.name))
                            currentIndex: paletteSelect.themes.findIndex(theme => theme.key === AppSettings.palette) + 1
                            Accessible.name: qsTr("Color theme")
                            onActivated: index => {
                                AppSettings.palette = index === 0 ? "" : paletteSelect.themes[index - 1].key
                            }
                        }
                    }
                    SelectSetting {
                        path: "appearance.reducedMotion"
                        labels: page.toggleLabels(AppSettings.systemReducedMotion)
                    }
                    SelectSetting {
                        path: "appearance.reducedTransparency"
                        labels: page.toggleLabels(AppSettings.systemReducedTransparency)
                    }
                }

                SettingCard {
                    title: qsTr("Language")
                    iconName: "languages"
                    tone: ""

                    LanguageSetting {
                        labels: ({ system: qsTr("Follow Windows"), zh_CN: "简体中文", en: "English" })
                    }
                }
            }

            Section {
                name: "extensions"
                title: qsTr("Extensions")
                titleReserve: page.titleReserve
                shown: page.shows(name, true)

                Repeater {
                    model: SettingsCatalog.extensionGroups

                    SettingCard {
                        id: extensionCard

                        required property var modelData
                        readonly property var extension: ExtensionHost.entries.find(entry => entry.id === modelData.id) ?? ({})
                        readonly property string lead: ExtensionLook.lead(extensionCard.extension)

                        title: modelData.name
                        iconName: ExtensionLook.icon(lead)
                        tone: ExtensionLook.tone(lead)

                        Repeater {
                            model: extensionCard.modelData.settings

                            delegate: DelegateChooser {
                                role: "control"

                                DelegateChoice {
                                    roleValue: "switch"

                                    SwitchSetting {
                                        required property var modelData

                                        path: modelData.path
                                    }
                                }
                                DelegateChoice {
                                    roleValue: "select"

                                    SelectSetting {
                                        id: choiceRow

                                        required property var modelData

                                        path: modelData.path
                                        labels: choiceRow.entry?.choiceLabels ?? ({})
                                    }
                                }
                                DelegateChoice {
                                    roleValue: "slider"

                                    SliderSetting {
                                        id: sliderRow

                                        required property var modelData

                                        path: modelData.path
                                        step: sliderRow.entry?.step ?? 1
                                        decimals: sliderRow.entry?.decimals ?? 0
                                    }
                                }
                                DelegateChoice {
                                    roleValue: "number"

                                    NumberSetting {
                                        id: numberRow

                                        required property var modelData

                                        path: modelData.path
                                        decimals: numberRow.entry?.decimals ?? 0
                                    }
                                }
                                DelegateChoice {
                                    roleValue: "text"

                                    TextSetting {
                                        required property var modelData

                                        path: modelData.path
                                    }
                                }
                            }
                        }
                    }
                }

                Repeater {
                    model: page.settingsPanels.length

                    SettingCard {
                        id: panelCard

                        required property int index
                        readonly property var panelEntry: page.settingsPanels[panelCard.index] ?? null

                        objectName: "extensionSettingsPanel"
                        custom: true
                        title: panelCard.panelEntry !== null ? panelCard.panelEntry.title : ""
                        note: panelCard.panelEntry !== null ? panelCard.panelEntry.extensionName : ""
                        iconName: panelCard.panelEntry !== null
                                  && (panelCard.panelEntry.icon !== "" || panelCard.panelEntry.iconPath !== "")
                                  ? panelCard.panelEntry.icon : "puzzle"
                        iconPath: panelCard.panelEntry !== null ? panelCard.panelEntry.iconPath : ""
                        tone: "azure"

                        UiPanelBlock {
                            objectName: "extensionSettingsPanelView"
                            width: parent ? parent.width : 0
                            height: implicitHeight
                            entry: panelCard.panelEntry
                            shown: page.visible && page.current === "extensions" && !SettingsCatalog.searching
                        }
                    }
                }
            }

            Section {
                name: "ai"
                title: qsTr("AI")
                info: [
                    qsTr("The assistant in the key editor answers questions and changes the keys, the logic and the "
                         + "blueprint of the profile you edit. Only when you send a message, it sends what you wrote, "
                         + "the profile with its logic and, when it needs one, a picture of the phone's screen, to "
                         + "the model service you choose here. Its changes wait in a draft until you apply them."),
                    qsTr("AI clients on this computer can use GameScrcpy through MCP: look up the script API, try "
                         + "logic on recordings, see a phone's picture, read the log, change profiles and run "
                         + "scripts. They connect through a named pipe that only your Windows account can open; no "
                         + "network port is opened.")
                ].join("\n\n")
                titleReserve: page.titleReserve
                shown: page.shows(name, true)

                AiSettings {
                    objectName: "aiSettings"
                    title: qsTr("AI model")
                    note: qsTr("Used by the AI assistant and by ai.ask in logic")
                }

                McpSettings {
                    objectName: "mcpSettings"
                    title: qsTr("Let other AI apps connect (MCP)")
                }
            }

            Section {
                name: "about"
                title: qsTr("About")
                titleReserve: page.titleReserve
                holdsSettings: false
                shown: page.shows(name, false)

                AboutPage {
                    onDiagnosticsRequested: page.diagnosticsRequested()
                }
            }

            Column {
                width: parent.width
                visible: SettingsCatalog.searching && SettingsCatalog.hitCount === 0
                topPadding: Theme.space.x4l
                spacing: Theme.space.lg

                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "search"
                    size: Theme.size.iconHero
                    color: Theme.colors.textTertiary
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    textFormat: Text.PlainText
                    text: SettingsCatalog.device === ""
                          ? qsTr("No setting matches “%1”.").arg(SettingsCatalog.query.trim())
                          : qsTr("No per-device setting matches “%1”.").arg(SettingsCatalog.query.trim())
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.body.size
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: SettingsCatalog.device !== ""
                    text: qsTr("Search the global defaults")
                    onClicked: SettingsCatalog.scoped = false
                }
            }
        }
    }

    component Section: Column {
        id: section

        required property string name
        property string title
        property string info
        property real titleReserve: 0
        property bool holdsSettings: true
        property bool shown: true
        default property alias cards: cardList.data
        property alias tools: toolRow.data
        readonly property var ownPaths: SettingsCatalog.pageOverrides[name] ?? []

        function forgetAll() {
            const device = SettingsCatalog.device
            for (const path of ownPaths) {
                if (path.startsWith("ext:"))
                    ExtensionHost.resetDeviceValue(device, path)
                else
                    AppSettings.resetDeviceValue(device, path)
            }
        }

        width: parent ? parent.width : implicitWidth
        visible: shown
        spacing: Theme.space.lg + Theme.space.xxs

        Item {
            width: parent.width
            height: Math.max(heading.implicitHeight, toolRow.implicitHeight)

            Column {
                id: heading

                width: parent.width - section.titleReserve - (toolRow.width > 0 ? toolRow.width + Theme.space.lg : 0)
                spacing: Theme.space.xxs

                Row {
                    width: parent.width
                    spacing: Theme.space.sm

                    Text {
                        width: Math.min(implicitWidth, parent.width - (pageInfo.visible ? pageInfo.width + parent.spacing : 0))
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                        text: section.title
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.title.size
                        font.weight: Theme.type.title.weight
                    }

                    InfoButton {
                        id: pageInfo

                        anchors.verticalCenter: parent.verticalCenter
                        visible: section.info !== ""
                        title: section.title
                        info: section.info
                    }
                }

                Row {
                    visible: section.ownPaths.length > 0 && !SettingsCatalog.searching
                    spacing: Theme.space.sm

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: qsTr("%n setting(s) of its own", "", section.ownPaths.length)
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                    }

                    Button {
                        anchors.verticalCenter: parent.verticalCenter
                        tone: "ghost"
                        small: true
                        iconName: "rotate-ccw"
                        text: qsTr("Restore all to the global defaults")
                        onClicked: section.forgetAll()
                    }
                }
            }

            Row {
                id: toolRow

                anchors.right: parent.right
                anchors.rightMargin: section.titleReserve
                y: (Theme.type.title.lineHeight - height) / 2
                spacing: Theme.space.sm
            }
        }

        Text {
            width: parent.width
            visible: section.holdsSettings && SettingsCatalog.device !== ""
                     && SettingsCatalog.perDevicePages.indexOf(section.name) < 0
            textFormat: Text.PlainText
            text: qsTr("Everything on this page applies to all devices.")
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.body.size
            wrapMode: Text.Wrap
        }

        Column {
            id: cardList

            width: parent.width
            spacing: Theme.space.md + Theme.space.xxs
        }
    }

    component ShortcutCard: Card {
        id: shortcutCard

        property string title
        property string iconName
        property string tone
        property bool device: false

        width: parent ? parent.width : implicitWidth
        visible: entries.count > 0
        padding: 0
        horizontalPadding: 0

        ShortcutModel {
            id: entries

            device: shortcutCard.device
            filtered: SettingsCatalog.searching
            hits: SettingsCatalog.hits
        }

        Column {
            width: parent.width

            Item {
                width: parent.width
                height: Theme.size.tile + Theme.space.md + Theme.space.xxs + Theme.space.md

                Tile {
                    id: cardTile

                    x: Theme.space.lg + Theme.space.xxs
                    y: Theme.space.md + Theme.space.xxs
                    iconName: shortcutCard.iconName
                    tone: shortcutCard.tone
                }

                Text {
                    anchors.left: cardTile.right
                    anchors.leftMargin: Theme.space.md
                    anchors.verticalCenter: cardTile.verticalCenter
                    textFormat: Text.PlainText
                    text: shortcutCard.title
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.body.size
                    font.weight: Font.DemiBold
                }
            }

            Repeater {
                model: entries

                delegate: ShortcutRow {
                    required property int index

                    width: parent ? parent.width : implicitWidth
                    divider: true
                }
            }
        }
    }

    component GroupItem: T.AbstractButton {
        id: groupItem

        property string iconName
        property bool current: false
        property int count: 0

        implicitWidth: implicitContentWidth + leftPadding + rightPadding
        implicitHeight: Theme.size.listItem
        leftPadding: Theme.space.sm + Theme.space.xxs
        rightPadding: leftPadding
        spacing: Theme.space.sm + Theme.space.xxs
        hoverEnabled: true
        font.pixelSize: Theme.type.label.size
        font.weight: current ? Font.DemiBold : Font.Normal

        Accessible.role: Accessible.PageTab
        Accessible.selected: current
        Accessible.description: count > 0 ? qsTr("%n setting(s) of its own", "", count) : ""

        contentItem: Item {
            implicitWidth: label.implicitWidth + (counter.visible ? counter.implicitWidth + groupItem.spacing : 0)
            implicitHeight: label.implicitHeight

            Row {
                id: label

                anchors.verticalCenter: parent.verticalCenter
                spacing: groupItem.spacing

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: groupItem.iconName
                    size: Theme.size.iconSmall
                    color: groupItem.current ? Theme.colors.accentText : Theme.colors.textSecondary
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: groupItem.text
                    font: groupItem.font
                    color: groupItem.current || groupItem.hovered ? Theme.colors.textPrimary : Theme.colors.textSecondary
                }
            }

            Text {
                id: counter

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: groupItem.count > 0
                textFormat: Text.PlainText
                text: groupItem.count
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
            }
        }

        background: Rectangle {
            id: face

            radius: Theme.radius.md
            color: groupItem.current ? Theme.colors.statePress : "transparent"

            StateLayer {
                anchors.fill: parent
                radius: face.radius
                hovered: groupItem.hovered && !groupItem.current
                pressed: groupItem.down
            }

            FocusRing {
                visible: groupItem.visualFocus
                baseRadius: face.radius
            }
        }
    }
}
