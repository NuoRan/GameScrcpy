pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Item {
    id: picker

    signal attentionWanted(string question)

    function question(): string {
        const asked = HardwareAccess.picker
        return asked.own === true ? qsTr("Your logic wants to use a device")
                                  : qsTr("%1 wants to use a device").arg(asked.requester ?? "")
    }

    function kindLabel(kind: string): string {
        switch (kind) {
        case "serial":
            return qsTr("Serial port")
        case "hid":
            return qsTr("USB HID device")
        case "ble":
            return qsTr("Bluetooth LE device")
        default:
            return kind
        }
    }

    function kindIcon(kind: string): string {
        switch (kind) {
        case "ble":
            return "bluetooth"
        case "hid":
            return "usb"
        default:
            return "plug"
        }
    }

    function sync() {
        const asked = HardwareAccess.picker
        if (dialog.visible) {
            if (asked.ticket === dialog.ticket)
                return
            dialog.answered = true
            dialog.close()
            return
        }
        if (asked.open !== true)
            return
        dialog.ticket = asked.ticket
        dialog.chosen = []
        dialog.answered = false
        dialog.open()
    }

    Connections {
        target: HardwareAccess

        function onPickerChanged() {
            Qt.callLater(picker.sync)
        }

        function onAttentionWanted() {
            picker.attentionWanted(picker.question())
        }
    }

    Component.onCompleted: sync()

    Dialog {
        id: dialog

        property string ticket
        property var chosen: []
        property bool answered: false
        readonly property var asked: HardwareAccess.picker
        readonly property var devices: dialog.asked.devices ?? []
        readonly property string kind: dialog.asked.kind ?? ""
        readonly property bool own: dialog.asked.own === true
        readonly property var picked: dialog.devices.filter(device => device.granted === true
                                                                      || dialog.chosen.indexOf(device.id) >= 0)
        readonly property real bodyLimit: (dialog.parent ? dialog.parent.height : 0) - 2 * Theme.space.x6l
                                          - dialog.implicitHeaderHeight - dialog.implicitFooterHeight
                                          - dialog.topPadding - dialog.bottomPadding

        function toggle(id: string, wanted: bool) {
            const next = dialog.chosen.filter(each => each !== id)
            if (wanted)
                next.push(id)
            dialog.chosen = next
        }

        objectName: "hardwarePicker"
        title: dialog.own ? qsTr("Your logic wants to use a device")
                          : qsTr("%1 wants to use a device").arg(dialog.asked.requester ?? "")
        onAccepted: {
            dialog.answered = true
            HardwareAccess.allow(dialog.picked.map(device => device.id))
        }
        onRejected: {
            dialog.answered = true
            HardwareAccess.cancelPicker()
        }
        onClosed: {
            if (!dialog.answered && HardwareAccess.picker.ticket === dialog.ticket)
                HardwareAccess.cancelPicker()
            Qt.callLater(picker.sync)
        }

        footer: Item {
            implicitHeight: buttons.implicitHeight + 2 * Theme.space.xl

            Row {
                id: buttons

                anchors.right: parent.right
                anchors.rightMargin: Theme.space.xxl
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.sm

                Button {
                    objectName: "hardwareCancel"
                    text: qsTr("Cancel")
                    onClicked: dialog.reject()
                }

                Button {
                    objectName: "hardwareAllow"
                    tone: "primary"
                    text: qsTr("Allow")
                    enabled: dialog.picked.length > 0
                    onClicked: dialog.accept()
                }
            }
        }

        Flickable {
            id: scroller

            width: dialog.availableWidth
            height: implicitHeight
            implicitHeight: Math.max(0, Math.min(body.implicitHeight, dialog.bodyLimit))
            contentWidth: width
            contentHeight: body.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            Column {
                id: body

                width: scroller.width
                spacing: Theme.space.md

                Text {
                    width: parent.width
                    textFormat: Text.PlainText
                    text: dialog.own
                          ? qsTr("Your logic can only open the devices you tick. You can take them back in Settings > General.")
                          : qsTr("It can only open the devices you tick. You can take them back on its card on the Extensions page.")
                    color: Theme.colors.textSecondary
                    font.pixelSize: Theme.type.body.size
                    wrapMode: Text.Wrap
                }

                Row {
                    objectName: "hardwareScanning"
                    visible: dialog.kind === "ble"
                    spacing: Theme.space.sm

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.size.pillDot
                        height: width
                        radius: width / 2
                        color: Theme.colors.accent
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: qsTr("Looking for Bluetooth devices nearby…")
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                    }
                }

                Column {
                    width: parent.width
                    visible: dialog.devices.length > 0
                    spacing: Theme.space.sm

                    Repeater {
                        model: dialog.devices

                        delegate: Rectangle {
                            id: row

                            required property var modelData
                            readonly property bool granted: row.modelData.granted === true
                            readonly property bool ticked: row.granted || dialog.chosen.indexOf(row.modelData.id) >= 0

                            objectName: "hardwareDevice"
                            width: parent ? parent.width : implicitWidth
                            height: Theme.size.settingsRow
                            radius: Theme.radius.md
                            color: row.ticked ? Theme.colors.accentSubtle : Theme.colors.bgSunken
                            border.width: Theme.size.stroke
                            border.color: row.ticked ? Theme.colors.accentLine : Theme.colors.strokeSubtle

                            MouseArea {
                                anchors.fill: parent
                                enabled: !row.granted
                                cursorShape: row.granted ? Qt.ArrowCursor : Qt.PointingHandCursor
                                onClicked: dialog.toggle(row.modelData.id, !row.ticked)
                            }

                            Checkbox {
                                id: box

                                objectName: "hardwareDeviceBox"
                                x: Theme.space.md
                                anchors.verticalCenter: parent.verticalCenter
                                checked: row.ticked
                                enabled: !row.granted
                                Accessible.name: row.modelData.name ?? ""
                                onToggled: {
                                    dialog.toggle(row.modelData.id, box.checked)
                                    box.checked = Qt.binding(() => row.ticked)
                                }
                            }

                            Icon {
                                id: kindIcon

                                x: box.x + box.width + Theme.space.md
                                anchors.verticalCenter: parent.verticalCenter
                                name: picker.kindIcon(row.modelData.kind ?? "")
                                size: Theme.size.icon
                                color: Theme.colors.textSecondary
                            }

                            Column {
                                x: kindIcon.x + kindIcon.width + Theme.space.md
                                width: (allowed.visible ? allowed.x - Theme.space.sm : row.width - Theme.space.md) - x
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.space.xxs

                                Text {
                                    width: parent.width
                                    textFormat: Text.PlainText
                                    text: row.modelData.name ?? ""
                                    color: Theme.colors.textPrimary
                                    font.pixelSize: Theme.type.label.size
                                    font.weight: Theme.type.label.weight
                                    elide: Text.ElideRight
                                }

                                Text {
                                    width: parent.width
                                    textFormat: Text.PlainText
                                    text: [picker.kindLabel(row.modelData.kind ?? ""), row.modelData.detail ?? ""]
                                          .filter(part => part !== "").join(" · ")
                                    color: Theme.colors.textTertiary
                                    font.pixelSize: Theme.type.caption.size
                                    elide: Text.ElideRight
                                }
                            }

                            StatusPill {
                                id: allowed

                                objectName: "hardwareAllowed"
                                x: row.width - width - Theme.space.md
                                anchors.verticalCenter: parent.verticalCenter
                                visible: row.granted
                                tone: "ok"
                                text: qsTr("Allowed")
                            }
                        }
                    }
                }

                Column {
                    objectName: "hardwareEmpty"
                    width: parent.width
                    visible: dialog.devices.length === 0
                    topPadding: Theme.space.md
                    bottomPadding: Theme.space.md
                    spacing: Theme.space.sm

                    Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: picker.kindIcon(dialog.kind)
                        size: Theme.size.iconLarge
                        color: Theme.colors.textTertiary
                    }

                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        textFormat: Text.PlainText
                        text: dialog.kind === "ble" ? qsTr("No Bluetooth device found yet")
                                                    : qsTr("No matching device is connected")
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.label.size
                        font.weight: Theme.type.label.weight
                        wrapMode: Text.Wrap
                    }

                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        textFormat: Text.PlainText
                        text: dialog.kind === "ble"
                              ? qsTr("Turn the device on and put it in pairing mode, and check that Bluetooth is on in Windows. Devices show up here as they are found.")
                              : qsTr("Plug the device in and it shows up here. If it still does not, try another cable or USB port, or install its driver.")
                        color: Theme.colors.textSecondary
                        font.pixelSize: Theme.type.caption.size
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
