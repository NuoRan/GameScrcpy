pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

SettingsSheet {
    id: sheet

    readonly property var extension: ExtensionHost.entries.find(entry => entry.id === "gamescrcpy.esp32"
                                                                 && entry.superseded !== true) ?? ({})
    readonly property var entries: SettingsCatalog.entries
    readonly property string linkState: board.state
    readonly property bool connected: sheet.linkState === "ready" || sheet.linkState === "legacy"
    readonly property bool busy: sheet.linkState === "connecting" || board.flashing
    readonly property bool inUse: sheet.linkState === "busy"
    readonly property bool wifiMode: AppSettings.values["esp32.link"] === "wifi"
    readonly property string summary: {
        if (board.flashing)
            return board.flashStage
        if (sheet.inUse || sheet.linkState === "trouble")
            return board.problem
        if (!sheet.connected)
            return sheet.linkState === "connecting" ? qsTr("Looking for the board…") : qsTr("Not connected")
        const parts = [board.chip !== "" ? board.chip : qsTr("ESP32 with the old firmware")]
        if (board.firmware !== "")
            parts.push(qsTr("firmware %1").arg(board.firmware))
        if (board.phone !== "")
            parts.push(board.phone)
        if (board.roundTripMs > 0)
            parts.push(qsTr("round trip %1 ms").arg(board.roundTripMs.toFixed(1)))
        return parts.join(" · ")
    }
    readonly property string pillText: {
        switch (sheet.linkState) {
        case "ready":
            return qsTr("Connected")
        case "legacy":
            return qsTr("Old firmware")
        case "connecting":
            return qsTr("Connecting")
        case "flashing":
            return qsTr("Flashing")
        case "busy":
            return qsTr("In use")
        case "lost":
            return qsTr("Lost")
        case "trouble":
            return qsTr("Not working")
        case "failed":
            return qsTr("Not found")
        default:
            return qsTr("Not connected")
        }
    }
    readonly property string pillTone: {
        switch (sheet.linkState) {
        case "ready":
            return "ok"
        case "legacy":
        case "busy":
            return "warn"
        case "connecting":
        case "flashing":
            return "live"
        case "lost":
        case "trouble":
        case "failed":
            return "bad"
        default:
            return "idle"
        }
    }
    readonly property string firmwareText: {
        if (board.firmware === "")
            return sheet.linkState === "legacy" ? qsTr("Old firmware") : qsTr("Connect the board to read it")
        if (board.upgradable)
            return qsTr("%1 · %2 available").arg(board.firmware).arg(board.bundledVersion)
        return board.bundled && board.firmware === board.bundledVersion ? qsTr("%1 · up to date").arg(board.firmware)
                                                                        : board.firmware
    }
    readonly property string port: {
        const value = String(AppSettings.values["esp32.port"] ?? "")
        return value === "" ? "auto" : value
    }
    readonly property var portChoices: {
        const choices = [{
            name: "auto",
            title: board.serial && board.where !== "" ? qsTr("Automatic · now %1").arg(board.where) : qsTr("Automatic")
        }]
        for (const listed of board.ports)
            choices.push({ name: listed.name, title: listed.title })
        if (sheet.port !== "auto" && !choices.some(choice => choice.name === sheet.port))
            choices.push({ name: sheet.port, title: qsTr("%1 (not plugged in)").arg(sheet.port) })
        return choices
    }
    readonly property int portIndex: Math.max(0, sheet.portChoices.findIndex(choice => choice.name === sheet.port))
    readonly property string address: String(AppSettings.values["esp32.address"] ?? "")

    function show() {
        board.refreshPorts()
        sheet.open()
    }

    function openFlash() {
        board.refreshPorts()
        flashDialog.port = board.defaultFlashPort()
        flashDialog.open()
    }

    function revertAddress() {
        addressField.errorText = ""
        addressField.text = Qt.binding(() => sheet.address)
    }

    function submitAddress() {
        const typed = addressField.text.trim()
        if (typed === sheet.address) {
            sheet.revertAddress()
            return
        }
        if (!AppSettings.setValue("esp32.address", typed)) {
            addressField.errorText = qsTr("Could not save this text")
            return
        }
        sheet.revertAddress()
    }

    objectName: "esp32SettingsSheet"
    title: qsTr("ESP32 touch box")
    subtitle: qsTr("Extension · %1 · changes apply at once").arg(sheet.extension.version ?? "")
    iconName: "usb"
    tone: "amber"
    statusText: sheet.pillText
    statusTone: sheet.pillTone
    alternativeText: qsTr("Wiring guide")
    onAlternativeChosen: Commands.trigger("app.help", "esp32-touch")
    onAddressChanged: sheet.revertAddress()

    Esp32Board {
        id: board
    }

    Rectangle {
        objectName: "esp32Status"
        Layout.fillWidth: true
        implicitHeight: strip.implicitHeight + 2 * (Theme.space.sm + Theme.space.xxs)
        radius: Theme.radius.lg
        color: Theme.colors.bgSunken
        border.width: Theme.size.stroke
        border.color: Theme.colors.strokeSubtle

        ColumnLayout {
            id: strip

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.space.md + Theme.space.xxs
            anchors.rightMargin: Theme.space.sm + Theme.space.xxs
            spacing: Theme.space.sm

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space.sm

                Icon {
                    name: "usb"
                    size: Theme.size.iconSmall
                    color: Theme.colors.textSecondary
                }

                Text {
                    objectName: "esp32Summary"
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    text: sheet.summary
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }

                Button {
                    objectName: "esp32Try"
                    small: true
                    tone: "ghost"
                    iconName: "tap-echo"
                    enabled: sheet.connected && !sheet.busy
                    text: qsTr("Try it")
                    toolTip: qsTr("Taps the middle of the phone's screen once")
                    onClicked: board.tryTap()
                }

                Button {
                    objectName: "esp32Connect"
                    small: true
                    enabled: !board.flashing && sheet.linkState !== "connecting" && !sheet.inUse
                    text: sheet.connected ? qsTr("Disconnect")
                                          : sheet.linkState === "connecting" ? qsTr("Connecting…") : qsTr("Connect")
                    onClicked: sheet.connected ? board.disconnectBoard() : board.connectBoard()
                }
            }

            Rectangle {
                id: track

                objectName: "esp32FlashProgress"
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.size.sliderTrack
                visible: board.flashing
                radius: height / 2
                color: Theme.colors.strokeSubtle

                Rectangle {
                    width: track.width * board.flashProgress
                    height: track.height
                    radius: height / 2
                    color: Theme.colors.accent
                }
            }
        }
    }

    Text {
        objectName: "esp32Parts"
        Layout.fillWidth: true
        visible: board.partsText !== ""
        text: board.partsText
        textFormat: Text.PlainText
        color: Theme.colors.textTertiary
        font.pixelSize: Theme.type.caption.size
        wrapMode: Text.Wrap
    }

    InlineAlert {
        objectName: "esp32Legacy"
        Layout.fillWidth: true
        visible: sheet.linkState === "legacy"
        tone: "warn"
        title: qsTr("Old firmware: flashing the new firmware is recommended")
        detail: board.problem !== "" ? board.problem
                                     : qsTr("With the old QineTouch firmware only touches work. Keys, the mouse, the "
                                            + "gamepad and Wi-Fi need the new firmware.")
        actionText: board.bundled ? qsTr("Flash firmware") : ""
        actionEnabled: !sheet.busy
        onActionTriggered: sheet.openFlash()
    }

    InlineAlert {
        objectName: "esp32Problem"
        Layout.fillWidth: true
        visible: board.problem !== "" && !board.flashing && !sheet.inUse && sheet.linkState !== "legacy"
        tone: "bad"
        title: board.problem
    }

    InlineAlert {
        objectName: "esp32FlashResult"
        Layout.fillWidth: true
        visible: !board.flashing && board.flashResult !== ""
        tone: board.flashFailed ? "bad" : "info"
        title: board.flashResult
    }

    SettingsGroup {
        objectName: "esp32Connection"
        title: qsTr("Connection")

        SettingsRow {
            objectName: "esp32Link"
            width: parent ? parent.width : implicitWidth
            title: qsTr("Connection mode")
            description: sheet.entries["esp32.link"]?.description ?? ""

            SegmentedControl {
                id: linkChoice

                model: [qsTr("Serial port"), qsTr("Wi-Fi")]
                currentIndex: sheet.wifiMode ? 1 : 0
                Accessible.name: qsTr("Connection mode")
                onActivated: index => {
                    AppSettings.setValue("esp32.link", index === 1 ? "wifi" : "serial")
                    linkChoice.currentIndex = Qt.binding(() => sheet.wifiMode ? 1 : 0)
                }
            }
        }

        SettingsRow {
            objectName: "esp32Port"
            width: parent ? parent.width : implicitWidth
            visible: !sheet.wifiMode
            divider: true
            title: qsTr("Serial port")
            description: sheet.entries["esp32.port"]?.description ?? ""

            Select {
                id: portSelect

                model: sheet.portChoices
                textRole: "title"
                currentIndex: sheet.portIndex
                Accessible.name: qsTr("Serial port")
                onActivated: index => {
                    AppSettings.setValue("esp32.port", sheet.portChoices[index].name)
                    portSelect.currentIndex = Qt.binding(() => sheet.portIndex)
                }
            }

            Button {
                objectName: "esp32RefreshPorts"
                small: true
                tone: "ghost"
                iconName: "refresh-cw"
                toolTip: qsTr("Look for serial ports again")
                onClicked: board.refreshPorts()
            }
        }

        SettingsRow {
            objectName: "esp32Address"
            width: parent ? parent.width : implicitWidth
            visible: sheet.wifiMode
            divider: true
            title: qsTr("Address")
            description: sheet.entries["esp32.address"]?.description ?? ""

            TextField {
                id: addressField

                implicitWidth: Theme.size.fieldWidth
                mono: true
                placeholderText: qsTr("Automatic")
                text: sheet.address
                Accessible.name: qsTr("Address")
                onEditingFinished: sheet.submitAddress()
                Keys.onEscapePressed: sheet.revertAddress()
            }
        }

        SettingsRow {
            objectName: "esp32WifiRow"
            width: parent ? parent.width : implicitWidth
            visible: board.hasWifi || !sheet.connected
            divider: true
            title: qsTr("Wi-Fi")

            Text {
                objectName: "esp32Wifi"
                Layout.maximumWidth: Theme.size.fieldWidth
                textFormat: Text.PlainText
                text: board.hasWifi ? board.wifi : qsTr("Connect the board to set it up")
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
                horizontalAlignment: Text.AlignRight
                wrapMode: Text.Wrap
            }

            Button {
                objectName: "esp32WifiSetup"
                small: true
                enabled: sheet.linkState === "ready" && board.serial && board.hasWifi && !sheet.busy
                text: qsTr("Set up Wi-Fi…")
                onClicked: {
                    wifiDialog.failed = false
                    wifiDialog.open()
                }
            }
        }
    }

    SettingsGroup {
        objectName: "esp32Firmware"
        title: qsTr("Firmware")

        SettingsRow {
            objectName: "esp32FirmwareRow"
            width: parent ? parent.width : implicitWidth
            title: qsTr("Firmware version")

            Text {
                objectName: "esp32FirmwareVersion"
                Layout.maximumWidth: Theme.size.fieldWidth
                textFormat: Text.PlainText
                text: sheet.firmwareText
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.caption.size
                horizontalAlignment: Text.AlignRight
                wrapMode: Text.Wrap
            }

            Button {
                objectName: "esp32Flash"
                small: true
                tone: "ghost"
                enabled: board.bundled && !sheet.busy && !sheet.inUse
                text: board.upgradable ? qsTr("Upgrade firmware…") : qsTr("Flash firmware…")
                toolTip: board.bundled ? qsTr("Firmware %1 comes with GameScrcpy").arg(board.bundledVersion)
                                       : qsTr("This copy of GameScrcpy has no ESP32 firmware with it")
                onClicked: sheet.openFlash()
            }
        }
    }

    Dialog {
        id: flashDialog

        property string port

        readonly property int portIndex: board.ports.findIndex(entry => entry.name === flashDialog.port)

        objectName: "esp32FlashDialog"
        title: board.upgradable ? qsTr("Upgrade the ESP32 firmware") : qsTr("Flash the ESP32 firmware")
        acceptText: board.upgradable ? qsTr("Upgrade") : qsTr("Flash")
        onAccepted: {
            if (flashDialog.port !== "")
                board.flash(flashDialog.port)
        }

        ColumnLayout {
            width: parent.width
            spacing: Theme.space.md

            Text {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: qsTr("Choose the serial port the board is plugged into on this computer. Firmware %1 is written; "
                           + "it takes about half a minute. Usually no button is needed.").arg(board.bundledVersion)
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.body.size
                wrapMode: Text.Wrap
            }

            Select {
                objectName: "esp32FlashPort"
                Layout.fillWidth: true
                visible: board.ports.length > 0
                model: board.ports
                textRole: "title"
                currentIndex: flashDialog.portIndex
                Accessible.name: qsTr("Serial port")
                onActivated: index => {
                    flashDialog.port = board.ports[index].name
                }
            }

            Text {
                Layout.fillWidth: true
                visible: board.ports.length === 0
                textFormat: Text.PlainText
                text: qsTr("No serial port found. Plug the board's serial port into this computer.")
                color: Theme.colors.warningText
                font.pixelSize: Theme.type.caption.size
                wrapMode: Text.Wrap
            }

            Text {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: qsTr("If it says the board did not enter download mode: hold BOOT while plugging in the cable, "
                           + "then flash again.")
                color: Theme.colors.textTertiary
                font.pixelSize: Theme.type.caption.size
                wrapMode: Text.Wrap
            }
        }
    }

    Dialog {
        id: wifiDialog

        property bool failed: false

        objectName: "esp32WifiDialog"
        title: qsTr("Wi-Fi for the ESP32 touch box")
        acceptText: qsTr("Save")
        alternativeText: board.wifiConfigured ? qsTr("Forget Wi-Fi") : ""
        onAboutToShow: {
            ssid.text = ""
            password.text = ""
        }
        onAccepted: {
            if (ssid.text.trim() === "" || !board.setUpWifi(ssid.text, password.text)) {
                wifiDialog.failed = true
                wifiDialog.open()
            }
        }
        onAlternativeChosen: board.forgetWifi()

        ColumnLayout {
            width: parent.width
            spacing: Theme.space.md

            Text {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                text: qsTr("The board joins this network and can then be reached without the cable. Only this "
                           + "computer gets the key that lets it talk to the board.")
                color: Theme.colors.textSecondary
                font.pixelSize: Theme.type.body.size
                wrapMode: Text.Wrap
            }

            TextField {
                id: ssid

                objectName: "esp32Ssid"
                Layout.fillWidth: true
                maximumLength: 32
                placeholderText: qsTr("Network name")
                errorText: wifiDialog.failed ? qsTr("Could not send the Wi-Fi settings to the board") : ""
                Accessible.name: qsTr("Network name")
            }

            TextField {
                id: password

                objectName: "esp32Password"
                Layout.fillWidth: true
                maximumLength: 64
                echoMode: TextInput.Password
                placeholderText: qsTr("Password")
                Accessible.name: qsTr("Password")
            }
        }
    }
}
