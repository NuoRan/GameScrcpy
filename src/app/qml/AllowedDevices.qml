pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Column {
    id: allowed

    required property string owner
    readonly property var devices: HardwareAccess.grants[allowed.owner] ?? []
    readonly property int inset: Theme.space.lg + Theme.space.xxs

    function iconOf(kind: string): string {
        switch (kind) {
        case "ble":
            return "bluetooth"
        case "hid":
            return "usb"
        default:
            return "plug"
        }
    }

    objectName: "allowedDevices"
    width: parent ? parent.width : 0
    visible: allowed.devices.length > 0

    Repeater {
        model: allowed.devices

        delegate: Item {
            id: deviceRow

            required property var modelData
            required property int index

            objectName: "allowedDevice"
            width: allowed.width
            implicitHeight: Math.max(Theme.size.settingsRow, line.implicitHeight + 2 * Theme.space.md)

            Rectangle {
                visible: deviceRow.index > 0
                x: allowed.inset
                width: deviceRow.width - 2 * allowed.inset
                height: Theme.size.stroke
                color: Theme.colors.strokeSubtle
            }

            RowLayout {
                id: line

                x: allowed.inset
                width: deviceRow.width - 2 * allowed.inset
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.space.md

                Icon {
                    name: allowed.iconOf(deviceRow.modelData.kind ?? "")
                    size: Theme.size.iconSmall
                    color: Theme.colors.textSecondary
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space.xxs

                    Text {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: deviceRow.modelData.name ?? ""
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.body.size
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }

                    Text {
                        objectName: "deviceDetail"
                        Layout.fillWidth: true
                        visible: text !== ""
                        textFormat: Text.PlainText
                        text: deviceRow.modelData.present === true ? deviceRow.modelData.detail ?? ""
                                                                    : qsTr("Not connected")
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.caption.size
                        elide: Text.ElideRight
                    }
                }

                Button {
                    objectName: "takeBackDevice"
                    small: true
                    tone: "ghost"
                    text: qsTr("Take back")
                    Accessible.description: deviceRow.modelData.name ?? ""
                    onClicked: HardwareAccess.forget(allowed.owner, deviceRow.modelData.id ?? "")
                }
            }
        }
    }
}
