pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Card {
    id: card

    required property string serial
    required property string name
    required property string pill
    required property string tone
    required property string meta
    required property string problem
    required property string action
    required property string guide
    required property string help
    required property int stage
    required property bool failed
    required property bool connected
    required property bool muted
    required property real volume
    required property int transport
    required property bool tunnelOffer
    required property var links
    required property var stats
    required property var thumbnail
    required property var capture
    required property var esp32
    required property var picture
    required property var provided

    readonly property var transports: ["device.transport.adb", "device.transport.wifi", "device.transport.auto"]
    readonly property bool isCapture: card.capture !== undefined && card.capture !== null
                                      && card.capture.isCapture === true
    readonly property bool isEsp32: card.esp32 !== undefined && card.esp32 !== null && card.esp32.isEsp32 === true
    readonly property bool isProvided: card.provided !== undefined && card.provided !== null
                                       && card.provided.isProvided === true
    readonly property bool isPhone: !card.isCapture && !card.isEsp32 && !card.isProvided
    readonly property var providedActions: card.isProvided ? card.provided.actions ?? [] : []
    readonly property var settingsAction: card.providedActions.find(each => each.id === "settings") ?? null
    readonly property var menuActions: card.providedActions.filter(each => each.id !== "settings")
    readonly property var providedJob: card.isProvided ? card.provided.job ?? {} : {}
    readonly property bool providedTrouble: card.isProvided && (card.provided.state === "busy"
                                                                || card.provided.state === "problem"
                                                                || card.providedJob.failed === true)
    readonly property string assignment: card.isCapture ? (card.capture.assignment ?? "standalone") : ""
    readonly property string pictureText: {
        if (!card.isPhone || card.picture === undefined || card.picture === null)
            return ""
        const inUse = card.picture.inUse ?? ""
        const shown = inUse !== "" ? inUse : card.picture.source ?? ""
        if (shown === "capture") {
            const name = card.picture.captureName ?? ""
            return name !== "" ? qsTr("Picture: capture card · %1").arg(name) : qsTr("Picture: capture card")
        }
        return shown === "none" ? qsTr("No picture") : ""
    }
    readonly property string summary: {
        if (card.isCapture) {
            if (card.assignment !== "assigned")
                return card.capture.summary ?? ""
            const phone = card.capture.phoneName ?? ""
            return qsTr("Shows %1").arg(phone !== "" ? phone : card.capture.phone ?? "")
        }
        if (card.isEsp32)
            return card.esp32.summary ?? ""
        if (card.isProvided) {
            const job = card.providedJob
            if (job.running === true) {
                const fraction = job.fraction ?? -1
                const step = fraction >= 0 ? qsTr("%1 · %2%").arg(job.title ?? "").arg(Math.round(fraction * 100))
                                           : job.title ?? ""
                return [step, job.text ?? ""].filter(text => text !== "").join(" · ")
            }
            if (job.failed === true)
                return qsTr("%1 did not work: %2").arg(job.title ?? "").arg(job.text ?? "")
            return [card.provided.summary ?? "",
                    card.provided.state !== "ready" ? card.provided.stateText ?? "" : "",
                    job.finished === true ? qsTr("%1 finished").arg(job.title ?? "") : ""]
                .filter(text => text !== "").join(" · ")
        }
        return [card.meta, card.pictureText].filter(text => text !== "").join(" · ")
    }

    signal captureSettingsRequested()
    signal captureAssignRequested()

    active: connected

    HoverHandler {
        id: cardHover
    }

    component DeviceCommand: CommandState {
        target: card.serial
    }

    DeviceCommand { id: mirrorCommand; command: "device.mirror" }
    DeviceCommand { id: cancelCommand; command: "device.cancel" }
    DeviceCommand { id: transportCommand; command: "device.transport.adb" }
    DeviceCommand { id: muteCommand; command: "device.mute" }
    DeviceCommand { id: showCommand; command: "device.showWindow" }
    DeviceCommand { id: disconnectCommand; command: "device.disconnect" }
    DeviceCommand { id: screenCommand; command: "device.screenOff" }
    DeviceCommand { id: followCommand; command: "device.orientation.follow" }
    DeviceCommand { id: portraitCommand; command: "device.orientation.portrait" }
    DeviceCommand { id: landscapeCommand; command: "device.orientation.landscape" }
    DeviceCommand { id: backCommand; command: "device.back" }
    DeviceCommand { id: homeCommand; command: "device.home" }
    DeviceCommand { id: appSwitchCommand; command: "device.appSwitch" }
    DeviceCommand { id: diagnosticsCommand; command: "device.diagnostics" }
    DeviceCommand { id: endWirelessCommand; command: "device.endWireless" }
    DeviceCommand { id: settingsCommand; command: "device.settings" }
    DeviceCommand { id: appsCommand; command: "device.apps" }
    DeviceCommand { id: filesCommand; command: "device.files" }

    component Figure: Row {
        id: figure

        required property string value
        required property string unit

        spacing: Theme.space.xs

        Text {
            id: number

            textFormat: Text.PlainText
            text: figure.value
            color: Theme.colors.textPrimary
            font.pixelSize: Theme.type.label.size
            font.weight: Font.DemiBold
            font.features: ({ "tnum": 1 })
        }

        Text {
            anchors.baseline: number.baseline
            textFormat: Text.PlainText
            text: figure.unit
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.caption.size
        }
    }

    ColumnLayout {
        width: card.availableWidth
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.lg

            Item {
                Layout.alignment: Qt.AlignTop
                implicitWidth: Theme.size.previewLong
                implicitHeight: Theme.size.phoneHeight

                Rectangle {
                    id: phoneFrame

                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Theme.size.phoneWidth
                    height: parent.height
                    visible: card.isPhone
                    radius: Theme.radius.lg
                    color: "transparent"
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.strokeStrong

                    Rectangle {
                        id: screen

                        anchors.fill: parent
                        anchors.margins: Theme.space.xs
                        radius: Theme.radius.sm
                        color: card.connected ? Theme.colors.accentSubtle : Theme.colors.bgSunken

                        ThumbnailView {
                            anchors.fill: parent
                            visible: card.connected
                            radius: screen.radius
                            image: card.thumbnail
                        }
                    }
                }

                Rectangle {
                    id: box

                    objectName: "deviceThumb"
                    anchors.fill: parent
                    visible: !card.isPhone
                    radius: Theme.radius.md
                    color: Theme.colors.bgSunken
                    border.width: Theme.size.stroke
                    border.color: Theme.colors.strokeDefault

                    Icon {
                        anchors.centerIn: parent
                        name: card.isEsp32 ? "usb"
                              : card.isProvided ? (card.provided.icon !== "" ? ExtensionPanels.iconOf(card.provided.icon) : "plug")
                              : "monitor-play"
                        customPath: card.isProvided ? card.provided.iconPath ?? "" : ""
                        size: Theme.size.iconLarge
                        color: card.isEsp32 || card.isProvided ? Theme.toneInk("amber") : Theme.colors.textTertiary
                    }

                    ThumbnailView {
                        anchors.fill: parent
                        anchors.margins: Theme.size.stroke
                        visible: card.isCapture && card.connected
                        radius: box.radius
                        image: card.thumbnail
                    }
                }

                Rectangle {
                    visible: card.connected
                    x: (card.isPhone ? phoneFrame.x + phoneFrame.width : parent.width) - width + Theme.space.xxs
                    y: -Theme.space.xxs
                    width: Theme.size.pillDot + Theme.space.xs
                    height: width
                    radius: width / 2
                    color: Theme.colors.accent
                }

                Checkbox {
                    id: pick

                    objectName: "deviceSelect"
                    x: (card.isPhone ? phoneFrame.x : 0) - Theme.space.xs
                    y: -Theme.space.xs
                    visible: DeviceManager.batch.selecting || cardHover.hovered || pick.checked || pick.visualFocus
                    checked: DeviceManager.batch.selected.includes(card.serial)
                    Accessible.name: qsTr("Select %1").arg(card.name)
                    onToggled: {
                        DeviceManager.batch.setSelected(card.serial, pick.checked)
                        pick.checked = Qt.binding(() => DeviceManager.batch.selected.includes(card.serial))
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: Theme.space.xs

                Row {
                    Layout.fillWidth: true
                    spacing: Theme.space.sm

                    Text {
                        id: nameText

                        objectName: "deviceName"
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, parent.width - status.width - parent.spacing)
                        textFormat: Text.PlainText
                        text: card.name
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.subtitle.size
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight

                        HoverHandler {
                            id: nameHover
                        }

                        Tooltip {
                            text: card.name
                            visible: nameText.truncated && nameHover.hovered
                        }
                    }

                    StatusPill {
                        id: status

                        anchors.verticalCenter: parent.verticalCenter
                        text: card.pill
                        tone: card.tone
                    }
                }

                Text {
                    objectName: "deviceSummary"
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    text: card.summary
                    color: (card.isEsp32 && card.esp32.inUse === true) || card.providedTrouble
                           ? Theme.colors.warningText : Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                    elide: Text.ElideRight
                }

                RowLayout {
                    objectName: "captureAsk"
                    Layout.fillWidth: true
                    visible: card.assignment === "ask"
                    spacing: Theme.space.xs

                    Text {
                        objectName: "captureAskText"
                        textFormat: Text.PlainText
                        text: qsTr("Which phone is this the picture of?")
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.caption.size
                        elide: Text.ElideRight
                    }

                    Button {
                        objectName: "captureChoosePhone"
                        small: true
                        tone: "ghost"
                        iconName: "link"
                        text: qsTr("Choose phone…")
                        onClicked: card.captureAssignRequested()
                    }

                    Item {
                        Layout.fillWidth: true
                    }
                }

                SegmentedControl {
                    id: linkChoice

                    objectName: "linkChoice"
                    Layout.topMargin: Theme.space.xxs
                    visible: card.links.length > 1 && transportCommand.enabled
                    model: card.links.map(link => ({ text: link.name, icon: link.wireless ? "wifi" : "plug" }))
                    currentIndex: card.links.findIndex(link => link.chosen)
                    onActivated: index => {
                        DeviceManager.chooseLink(card.links[index].serial)
                        linkChoice.currentIndex = Qt.binding(() => card.links.findIndex(link => link.chosen))
                    }
                }

                Row {
                    Layout.topMargin: Theme.space.sm
                    visible: card.connected && card.stats.fps !== undefined
                    spacing: Theme.space.lg + Theme.space.xxs

                    Figure {
                        value: Math.round(card.stats.fps ?? 0).toString()
                        unit: qsTr("fps")
                    }
                    Figure {
                        value: card.stats.latencyMs !== undefined ? Math.round(card.stats.latencyMs).toString() : "–"
                        unit: qsTr("ms latency")
                    }
                    Figure {
                        value: (card.stats.megabits ?? 0).toFixed(1)
                        unit: qsTr("Mbit/s")
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space.xs
                    visible: card.problem !== ""
                    textFormat: Text.PlainText
                    text: card.problem
                    color: card.failed ? Theme.colors.dangerText : Theme.colors.warningText
                    font.pixelSize: Theme.type.body.size
                    font.weight: Font.Medium
                    wrapMode: Text.Wrap
                }

                Text {
                    Layout.fillWidth: true
                    visible: card.action !== ""
                    textFormat: Text.PlainText
                    text: card.action
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }

                Button {
                    Layout.topMargin: Theme.space.xxs
                    Layout.leftMargin: -Theme.space.md
                    visible: card.tunnelOffer
                    small: true
                    tone: "ghost"
                    iconName: "plug"
                    text: qsTr("Use adb tunnel")
                    onClicked: {
                        DeviceManager.setTransport(card.serial, 0)
                        DeviceManager.connectDevice(card.serial)
                    }
                }

                Button {
                    Layout.topMargin: Theme.space.xxs
                    Layout.leftMargin: -Theme.space.md
                    visible: card.guide !== ""
                    small: true
                    tone: "ghost"
                    iconName: "circle-help"
                    text: qsTr("View guide")
                    onClicked: {
                        const parts = card.guide.split("/")
                        SetupGuide.open(1, parts[0], parts[1] ?? "")
                    }
                }

                Button {
                    objectName: "driverGuide"
                    Layout.topMargin: Theme.space.xxs
                    Layout.leftMargin: -Theme.space.md
                    visible: card.help !== ""
                    small: true
                    tone: "ghost"
                    iconName: "circle-help"
                    text: qsTr("How to install the driver")
                    onClicked: Commands.trigger("app.help", card.help)
                }
            }

            RowLayout {
                spacing: Theme.space.sm

                SegmentedControl {
                    id: transportChoice

                    objectName: "deviceTransport"
                    visible: transportCommand.enabled && card.isPhone
                    model: [
                        { text: qsTr("adb tunnel"), icon: "plug" },
                        { text: qsTr("WiFi direct"), icon: "wifi" },
                        { text: qsTr("Auto") }
                    ]
                    currentIndex: card.transport
                    onActivated: index => {
                        Commands.trigger(card.transports[index], card.serial)
                        transportChoice.currentIndex = Qt.binding(() => card.transport)
                    }
                }

                Button {
                    objectName: card.isEsp32 ? "esp32Control" : card.isProvided ? "providedControl" : "deviceMirror"
                    visible: mirrorCommand.enabled
                    tone: "primary"
                    iconName: card.isEsp32 || card.isProvided ? "mouse-pointer-click" : mirrorCommand.icon
                    text: card.failed ? qsTr("Try again")
                          : card.isEsp32 || card.isProvided ? qsTr("Control") : mirrorCommand.title
                    toolTip: card.isEsp32 ? qsTr("Opens a window without a picture: the keys, mouse and keyboard work "
                                                 + "the phone through the board while you watch the phone")
                             : card.isProvided ? qsTr("Opens a window without a picture: the keys, mouse and keyboard "
                                                      + "work the phone through %1").arg(card.name)
                             : ""
                    onClicked: mirrorCommand.trigger()
                }

                Button {
                    visible: cancelCommand.enabled
                    tone: "ghost"
                    text: qsTr("Cancel")
                    onClicked: cancelCommand.trigger()
                }

                Button {
                    visible: muteCommand.enabled
                    tone: "ghost"
                    iconName: muteCommand.icon
                    toolTip: muteCommand.title
                    onClicked: muteCommand.trigger()
                }

                Slider {
                    visible: muteCommand.enabled
                    implicitWidth: Theme.size.sliderCompact + valueWidth
                    enabled: !card.muted
                    from: 0
                    to: 1
                    value: card.volume
                    valueText: card.muted ? qsTr("Muted") : Math.round(value * 100) + "%"
                    onMoved: DeviceManager.setVolume(card.serial, value)
                }

                Button {
                    visible: showCommand.enabled
                    iconName: showCommand.icon
                    text: showCommand.title
                    onClicked: showCommand.trigger()
                }

                Button {
                    visible: disconnectCommand.enabled
                    tone: "ghost"
                    text: disconnectCommand.title
                    onClicked: disconnectCommand.trigger()
                }

                Button {
                    id: settingsButton

                    objectName: card.isCapture ? "captureSettings" : card.isEsp32 ? "esp32Settings"
                                : card.isProvided ? "providedSettings" : "deviceSettings"
                    visible: card.isProvided ? card.settingsAction !== null : !card.isPhone || settingsCommand.enabled
                    tone: "ghost"
                    iconName: card.isPhone ? settingsCommand.icon : "sliders-horizontal"
                    toolTip: card.isPhone ? settingsCommand.title
                             : card.settingsAction !== null ? card.settingsAction.title : qsTr("Settings")
                    Accessible.name: settingsButton.toolTip
                    onClicked: {
                        if (card.isCapture)
                            card.captureSettingsRequested()
                        else if (card.isEsp32)
                            Commands.trigger("esp32.settings", card.serial)
                        else if (card.isProvided)
                            DeviceManager.runDeviceAction(card.serial, "settings")
                        else
                            settingsCommand.trigger()
                    }
                }

                Button {
                    id: moreButton

                    readonly property bool ownItems: appsCommand.enabled || filesCommand.enabled || screenCommand.enabled
                                                     || followCommand.enabled || backCommand.enabled
                                                     || endWirelessCommand.enabled || card.isCapture || card.isEsp32
                                                     || card.menuActions.length > 0

                    objectName: "deviceMore"
                    visible: moreButton.ownItems || extensionItems.count > 0
                    tone: "ghost"
                    iconName: "ellipsis"
                    toolTip: qsTr("More")
                    onClicked: more.open()

                    Menu {
                        id: more

                        x: moreButton.width - width
                        y: moreButton.height + Theme.space.xs

                        Instantiator {
                            model: card.menuActions.length

                            delegate: MenuItem {
                                id: actionItem

                                required property int index
                                readonly property var deviceAction: card.menuActions[actionItem.index] ?? null

                                objectName: "providedAction"
                                iconName: actionItem.deviceAction === null ? ""
                                        : actionItem.deviceAction.job !== "" ? "play"
                                        : actionItem.deviceAction.panel !== "" ? "panel-top" : "zap"
                                text: actionItem.deviceAction?.title ?? ""
                                enabled: actionItem.deviceAction !== null
                                         && !(actionItem.deviceAction.job !== "" && card.providedJob.running === true)
                                onTriggered: {
                                    if (actionItem.deviceAction !== null)
                                        DeviceManager.runDeviceAction(card.serial, actionItem.deviceAction.id)
                                }
                            }

                            onObjectAdded: (index, object) => more.insertItem(index, object)
                            onObjectRemoved: (index, object) => more.removeItem(object)
                        }

                        CommandMenuItem {
                            objectName: "deviceApps"
                            commandState: appsCommand
                        }
                        CommandMenuItem {
                            objectName: "deviceFiles"
                            commandState: filesCommand
                        }
                        MenuSeparator {
                            visible: (appsCommand.enabled || filesCommand.enabled)
                                     && (screenCommand.enabled || followCommand.enabled || backCommand.enabled)
                            height: visible ? implicitHeight : 0
                        }
                        CommandMenuItem { commandState: screenCommand }
                        CommandMenuItem { commandState: followCommand; choice: true }
                        CommandMenuItem { commandState: portraitCommand; choice: true }
                        CommandMenuItem { commandState: landscapeCommand; choice: true }
                        MenuSeparator {
                            visible: backCommand.enabled
                            height: visible ? implicitHeight : 0
                        }
                        CommandMenuItem { commandState: backCommand }
                        CommandMenuItem { commandState: homeCommand }
                        CommandMenuItem { commandState: appSwitchCommand }
                        MenuSeparator {
                            visible: diagnosticsCommand.enabled
                            height: visible ? implicitHeight : 0
                        }
                        CommandMenuItem { commandState: diagnosticsCommand; choice: true }
                        MenuSeparator {
                            visible: backCommand.enabled && endWirelessCommand.enabled
                            height: visible ? implicitHeight : 0
                        }
                        CommandMenuItem { commandState: endWirelessCommand }
                        MenuSeparator {
                            visible: card.isCapture && (backCommand.enabled || diagnosticsCommand.enabled)
                            height: visible ? implicitHeight : 0
                        }
                        MenuItem {
                            objectName: "assignCaptureCard"
                            visible: card.assignment === "standalone" || card.assignment === "assigned"
                            height: visible ? implicitHeight : 0
                            iconName: "link"
                            text: qsTr("Assign to a phone…")
                            onTriggered: card.captureAssignRequested()
                        }
                        MenuItem {
                            objectName: "hideCaptureCard"
                            visible: card.isCapture && card.capture.demo !== true
                            height: visible ? implicitHeight : 0
                            iconName: "eye"
                            text: qsTr("This is not a capture card, hide it")
                            onTriggered: DeviceManager.hideCaptureCard(card.serial)
                        }
                        MenuItem {
                            objectName: "captureGuide"
                            visible: card.isCapture
                            height: visible ? implicitHeight : 0
                            iconName: "circle-help"
                            text: qsTr("About capture cards")
                            onTriggered: Commands.trigger("app.help", "capture-card")
                        }
                        MenuSeparator {
                            visible: card.isEsp32 && backCommand.enabled
                            height: visible ? implicitHeight : 0
                        }
                        MenuItem {
                            objectName: "esp32Disconnect"
                            visible: card.isEsp32
                            height: visible ? implicitHeight : 0
                            iconName: "unlink"
                            text: qsTr("Disconnect the board")
                            description: qsTr("Lets go of the board until it is plugged in again or connected in its "
                                              + "settings")
                            onTriggered: DeviceManager.disconnectBoard(card.serial)
                        }
                        MenuSeparator {
                            visible: extensionItems.count > 0 && moreButton.ownItems
                            height: visible ? implicitHeight : 0
                        }

                        UiCardMenuItems {
                            id: extensionItems

                            menu: more
                            kind: "device"
                            target: card.serial
                            targetName: card.name
                        }
                    }
                }
            }
        }

        ConnectionStepper {
            Layout.topMargin: Theme.space.sm
            Layout.leftMargin: Theme.size.previewLong + Theme.space.lg
            visible: card.stage >= 0 && !card.isCapture && !card.isEsp32 && !card.isProvided
            current: card.stage
            failed: card.failed
        }
    }
}
