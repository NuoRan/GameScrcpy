pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Column {
    id: section

    property real now: Date.now() / 1000
    property bool typedPairing: false
    property bool focusWanted: false

    readonly property var nearby: WirelessController.nearby
    readonly property string pairTarget: typedPairing
                                         ? pairingAddress.text.trim()
                                         : nearby.length > 0
                                           ? nearby[Math.max(0, Math.min(phoneChoice.currentIndex, nearby.length - 1))].address
                                           : ""

    function connectTyped() {
        if (addressField.text.trim() !== "")
            WirelessController.connectTo(addressField.text)
    }

    function focusPairing() {
        if (code.enabled)
            code.forceActiveFocus(Qt.OtherFocusReason)
        else if (section.typedPairing)
            pairingAddress.forceActiveFocus(Qt.OtherFocusReason)
        else
            section.focusWanted = true
    }

    function since(lastUsed) {
        const minutes = Math.floor((section.now - lastUsed) / 60)
        if (minutes < 1)
            return qsTr("just now")
        if (minutes < 60)
            return qsTr("%n minute(s) ago", "", minutes)
        const hours = Math.floor(minutes / 60)
        if (hours < 24)
            return qsTr("%n hour(s) ago", "", hours)
        const days = Math.floor(hours / 24)
        return days === 1 ? qsTr("yesterday") : qsTr("%n day(s) ago", "", days)
    }

    spacing: Theme.space.md

    Timer {
        interval: 30000
        repeat: true
        running: section.visible && !AppSettings.wirelessCollapsed
        onTriggered: section.now = Date.now() / 1000
    }

    Binding {
        target: WirelessController
        property: "discovering"
        value: section.visible && !AppSettings.wirelessCollapsed
    }

    Connections {
        target: WirelessController

        function onPairingChanged() {
            if (WirelessController.pairProblem !== "")
                code.text = ""
        }
    }

    Connections {
        target: AppSettings

        function onWirelessChanged() {
            if (AppSettings.wirelessCollapsed)
                section.focusWanted = false
        }
    }

    Item {
        width: section.width
        implicitHeight: toggle.implicitHeight

        Text {
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: qsTr("Wireless debugging")
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.label.size
            font.weight: Theme.type.label.weight
        }

        Button {
            id: toggle

            anchors.right: parent.right
            tone: "ghost"
            small: true
            text: AppSettings.wirelessCollapsed ? qsTr("Show") : qsTr("Hide")
            onClicked: AppSettings.wirelessCollapsed = !AppSettings.wirelessCollapsed
        }
    }

    RowLayout {
        width: section.width
        visible: !AppSettings.wirelessCollapsed
        spacing: Theme.space.md

        Card {
            id: pairingCard

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 0
            Layout.horizontalStretchFactor: 115

            ColumnLayout {
                width: pairingCard.availableWidth
                spacing: Theme.space.xs

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space.sm

                    Icon {
                        name: "wifi"
                        size: Theme.size.iconSmall
                        color: Theme.colors.textSecondary
                    }

                    Text {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: WirelessController.qrText === "" ? qsTr("Pair with a pairing code") : qsTr("Pair with a QR code")
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.body.size
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Button {
                        visible: WirelessController.qrText === ""
                        small: true
                        tone: "ghost"
                        iconName: "qr-code"
                        text: qsTr("Use a QR code")
                        onClicked: WirelessController.showQrCode()
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: WirelessController.qrText === ""
                    textFormat: Text.PlainText
                    text: qsTr("On the phone: Developer options > Wireless debugging > Pair device with pairing "
                               + "code. Then type the six digits it shows.")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space.sm
                    visible: WirelessController.qrText === ""
                    spacing: Theme.space.sm

                    Text {
                        Layout.fillWidth: true
                        visible: !section.typedPairing && section.nearby.length <= 1
                        textFormat: Text.PlainText
                        text: section.nearby.length === 0
                              ? qsTr("Looking for a phone that shows its pairing dialog…")
                              : qsTr("Found %1 at %2").arg(section.nearby[0].label).arg(section.nearby[0].address)
                        color: section.nearby.length === 0 ? Theme.colors.textTertiary : Theme.colors.textSecondary
                        font.pixelSize: Theme.type.caption.size
                        elide: Text.ElideRight
                    }

                    Select {
                        id: phoneChoice

                        Layout.fillWidth: true
                        visible: !section.typedPairing && section.nearby.length > 1
                        model: section.nearby.map(phone => phone.label + "  " + phone.address)
                    }

                    TextField {
                        id: pairingAddress

                        Layout.fillWidth: true
                        visible: section.typedPairing
                        mono: true
                        placeholderText: qsTr("IP address and port of the pairing dialog")
                    }

                    Button {
                        small: true
                        tone: "ghost"
                        text: section.typedPairing ? qsTr("Find it nearby") : qsTr("Type the address")
                        onClicked: section.typedPairing = !section.typedPairing
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space.sm
                    visible: WirelessController.qrText === ""
                    spacing: Theme.space.md

                    CodeField {
                        id: code

                        description: qsTr("Pairing code")
                        enabled: section.pairTarget !== "" && WirelessController.pairingAddress === ""
                        onCompleted: digits => WirelessController.pair(section.pairTarget, digits)
                        onEnabledChanged: {
                            if (code.enabled && section.focusWanted) {
                                section.focusWanted = false
                                code.forceActiveFocus(Qt.OtherFocusReason)
                            }
                        }
                    }

                    Button {
                        visible: WirelessController.pairingAddress !== ""
                        tone: "ghost"
                        text: qsTr("Cancel")
                        onClicked: WirelessController.cancelPairing()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space.sm
                    visible: WirelessController.qrText !== ""
                    spacing: Theme.space.lg

                    QrCodeView {
                        modules: WirelessController.qrSize
                        path: WirelessController.qrPath
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: Theme.space.sm

                        Text {
                            Layout.fillWidth: true
                            textFormat: Text.PlainText
                            text: qsTr("On the phone: Developer options > Wireless debugging > Pair device with QR "
                                       + "code, then scan this code. Pairing starts once the phone has scanned it.")
                            color: Theme.colors.textSecondary
                            font.pixelSize: Theme.type.caption.size
                            wrapMode: Text.Wrap
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: WirelessController.pairingAddress === "" && WirelessController.pairedAddress === ""
                                     && WirelessController.pairProblem === ""
                            textFormat: Text.PlainText
                            text: qsTr("Waiting for the phone to scan the code…")
                            color: Theme.colors.textTertiary
                            font.pixelSize: Theme.type.caption.size
                            wrapMode: Text.Wrap
                        }

                        RowLayout {
                            spacing: Theme.space.sm

                            Button {
                                visible: WirelessController.pairingAddress !== ""
                                small: true
                                tone: "ghost"
                                text: qsTr("Cancel")
                                onClicked: WirelessController.cancelPairing()
                            }

                            Button {
                                small: true
                                tone: "ghost"
                                text: qsTr("Use a pairing code")
                                onClicked: WirelessController.hideQrCode()
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: WirelessController.pairingAddress !== ""
                    textFormat: Text.PlainText
                    text: qsTr("Pairing with %1…").arg(WirelessController.pairingAddress)
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                    elide: Text.ElideRight
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: WirelessController.pairingAddress === "" && WirelessController.pairedAddress !== ""
                    spacing: Theme.space.xxs

                    Text {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: qsTr("Paired with %1.").arg(WirelessController.pairedAddress)
                        color: Theme.colors.successText
                        font.pixelSize: Theme.type.label.size
                        font.weight: Theme.type.label.weight
                        wrapMode: Text.Wrap
                    }

                    Text {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: qsTr("The phone usually connects by itself and appears in the list above. If it does "
                                   + "not, connect to the IP address and port on its Wireless debugging screen, next "
                                   + "to this.")
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.caption.size
                        wrapMode: Text.Wrap
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: WirelessController.pairingAddress === "" && WirelessController.pairProblem !== ""
                    spacing: Theme.space.xxs

                    Text {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: WirelessController.pairProblem
                        color: Theme.colors.dangerText
                        font.pixelSize: Theme.type.label.size
                        font.weight: Theme.type.label.weight
                        wrapMode: Text.Wrap
                    }

                    Text {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: WirelessController.pairAction
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.caption.size
                        wrapMode: Text.Wrap
                    }

                    Text {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: qsTr("adb: %1").arg(WirelessController.pairDetail)
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                        wrapMode: Text.Wrap
                    }
                }
            }
        }

        Card {
            id: recent

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 0
            Layout.horizontalStretchFactor: 100

            ColumnLayout {
                width: recent.availableWidth
                spacing: Theme.space.xs

                RowLayout {
                    spacing: Theme.space.sm

                    Icon {
                        name: "history"
                        size: Theme.size.iconSmall
                        color: Theme.colors.textSecondary
                    }

                    Text {
                        textFormat: Text.PlainText
                        text: qsTr("Recent connections")
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.body.size
                        font.weight: Font.DemiBold
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: AppSettings.recentConnections.length === 0
                    textFormat: Text.PlainText
                    text: qsTr("Phones you connect to over wireless debugging are listed here.")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }

                Repeater {
                    model: AppSettings.recentConnections

                    delegate: Item {
                        id: entry

                        required property var modelData
                        required property int index
                        readonly property bool online: DeviceManager.onlineSerials.indexOf(modelData.address) >= 0

                        Layout.fillWidth: true
                        implicitHeight: row.implicitHeight + 2 * Theme.space.sm

                        Rectangle {
                            width: parent.width
                            height: Theme.size.stroke
                            visible: entry.index > 0
                            color: Theme.colors.strokeSubtle
                        }

                        RowLayout {
                            id: row

                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.space.md

                            Text {
                                Layout.minimumWidth: 0
                                textFormat: Text.PlainText
                                text: entry.modelData.name !== "" ? entry.modelData.name : qsTr("Wireless device")
                                color: Theme.colors.textPrimary
                                font.pixelSize: Theme.type.label.size
                                elide: Text.ElideRight
                            }

                            Text {
                                textFormat: Text.PlainText
                                text: entry.modelData.address
                                color: Theme.colors.textSecondary
                                font.family: Theme.type.monoFamily
                                font.pixelSize: Theme.type.caption.size
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            Text {
                                textFormat: Text.PlainText
                                text: entry.online ? qsTr("Connected") : section.since(entry.modelData.lastUsed)
                                color: entry.online ? Theme.colors.successText : Theme.colors.textTertiary
                                font.pixelSize: Theme.type.caption.size
                            }

                            Button {
                                visible: !entry.online
                                small: true
                                enabled: WirelessController.pendingAddress === ""
                                text: qsTr("Connect")
                                onClicked: WirelessController.connectTo(entry.modelData.address)
                            }

                            Button {
                                small: true
                                tone: "ghost"
                                iconName: "x"
                                toolTip: qsTr("Remove from the list")
                                onClicked: AppSettings.forgetConnection(entry.modelData.address)
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space.sm
                    spacing: Theme.space.sm

                    TextField {
                        id: addressField

                        Layout.fillWidth: true
                        mono: true
                        enabled: WirelessController.pendingAddress === ""
                        placeholderText: qsTr("IP address and port, such as 192.168.1.5:5555")
                        onAccepted: section.connectTyped()
                    }

                    Button {
                        visible: WirelessController.pendingAddress === ""
                        enabled: addressField.text.trim() !== ""
                        iconName: "link"
                        text: qsTr("Connect")
                        onClicked: section.connectTyped()
                    }

                    Button {
                        visible: WirelessController.pendingAddress !== ""
                        tone: "ghost"
                        text: qsTr("Cancel")
                        onClicked: WirelessController.cancel()
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: WirelessController.pendingAddress !== ""
                    textFormat: Text.PlainText
                    text: qsTr("Connecting to %1…").arg(WirelessController.pendingAddress)
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.caption.size
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    visible: WirelessController.pendingAddress === ""
                             && DeviceManager.onlineSerials.indexOf(WirelessController.connectedAddress) >= 0
                    textFormat: Text.PlainText
                    text: qsTr("Connected to %1; it is in the list above.").arg(WirelessController.connectedAddress)
                    color: Theme.colors.successText
                    font.pixelSize: Theme.type.caption.size
                    wrapMode: Text.Wrap
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: WirelessController.problem !== ""
                    spacing: Theme.space.xxs

                    Text {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: WirelessController.problem
                        color: Theme.colors.dangerText
                        font.pixelSize: Theme.type.label.size
                        font.weight: Theme.type.label.weight
                        wrapMode: Text.Wrap
                    }

                    Text {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: WirelessController.action
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.caption.size
                        wrapMode: Text.Wrap
                    }

                    Text {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: qsTr("adb: %1").arg(WirelessController.detail)
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
